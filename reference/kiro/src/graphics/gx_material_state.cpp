// Address range: 0x8016c594 - 0x8016de80 | batch_443 | 2026-09-06
//
// GX material / TEV render-state configuration layer.  This is the code that
// programs the GameCube GX pipeline per material: TEV colour/alpha stages
// (FUN_80181cf0/e58/ec8/d50/dd4), blend mode (FUN_803cab98), alpha compare
// (FUN_803ca2f8), TEV constant/konst registers (FUN_803ca238/1d4/29c) and the
// per-frame commit of pending matrix / projection / viewport / fog / scissor
// state.  It is driven each frame from the render-state flush (FUN_80172664 /
// dispatch_5690).
//
//   gx_set_blend_mode_tagged    0x8016c594  set blend/TEV mode from spawn-frame tag
//   gx_render_state_flush       0x8016c6e8  per-frame commit of pending GX state
//   gx_setup_env_tex_matrices   0x8016c8fc  build camera env-map/proj texture matrices
//   gx_material_render_glow     0x8016c9c8  render sub-object glow via glow RT texture
//   gx_build_proj_tex_matrix    0x8016ccfc  build projected-texture matrix (slot 0x33)
//   gx_indirect_stage_toggle    0x8016cdcc  toggle indirect-texture stage
//   gx_material_setup_tev       0x8016ce28  MASSIVE per-material TEV stage programming
//   gx_material_blend_by_type   0x8016d868  blend/TEV setup by material type id
//   gx_material_blend_by_type_b 0x8016ddc0  blend-mode variant switch
//   gx_material_apply           0x8016de80  top-level material-apply entry (spawn-frame)
#include "common.h"
#include "generated_decls.h"

// ---- render-state globals ----
extern unsigned char DAT_80612277;   // prev blend-mode tag (change detect)
extern unsigned char DAT_80612278;   // current blend-mode tag
extern unsigned char DAT_80612323;   // suppress-flush flag
extern unsigned char DAT_80612318;   // 2D/ortho mode current
extern unsigned char DAT_80612319;   // 2D/ortho mode prev
extern int           DAT_80612250;   // model/view-matrix dirty
extern int           DAT_8061225c;   // texture-matrix-share flag
extern int           DAT_80612310;   // projection-matrix dirty
extern int           DAT_80612314;   // clear/scissor-params dirty
extern unsigned char DAT_80612321;   // projection matrix slot
extern int           DAT_80612254;   // active material record ptr
extern unsigned int  DAT_806109d0;   // active material id (change detect)
extern unsigned int  DAT_806109dc;   // sub-material id
extern unsigned int  DAT_806109d8;   // material variant id
extern unsigned char DAT_80612262;
extern unsigned int  DAT_80610a04;   // pending fog params
extern unsigned int  DAT_80610a08;   // applied fog params
extern unsigned char DAT_806109e0;   // pending z-mode
extern unsigned char DAT_806109e1;   // applied z-mode
extern char          DAT_80610a00;   // active indirect stage (-1 none)
extern unsigned char DAT_80612288;   // env-tex reset flag
extern unsigned char DAT_80612261;   // shadow/overlay enable
extern unsigned char DAT_80612263;   // applied cull mode (change detect)
extern unsigned char DAT_80612264;   // indirect-stage-used flag
extern unsigned char DAT_806109f8;   // cached material result
extern unsigned char DAT_806109f9;   // applied z-compare (change detect)
extern unsigned char DAT_80610a0c;   // TEV konst-alpha selector
extern int           DAT_80610a20;   // texture-object table base

// spawn-frame callback slots (function pointers)
extern int (*DAT_80612284)(int, unsigned int*, unsigned int*);  // material pre-stage cb
extern int (*DAT_8061227c)(int, unsigned int*, unsigned int*);  // material override cb
extern int (*DAT_80612280)(int, unsigned int*, unsigned int*);  // material post-stage cb

// ---- rodata constants ----
extern float FLOAT_806164f8;   // 0.0
extern float FLOAT_806164fc;   // 1.0
extern unsigned int DAT_806164f4;  // packed colour 0xffffff80
extern float FLOAT_80616510;   // -1.0
extern float FLOAT_80616514;   // 0.5
extern float FLOAT_80616518;   // 2.0
extern float FLOAT_80616520;   // 0.45
extern float FLOAT_80616524;   // -0.45
extern float FLOAT_80616528;   // 0.25
extern float FLOAT_80616538;   // 176.0
extern float FLOAT_8061653c;   // 255.0
extern float FLOAT_806123d0;   // dynamic shadow intensity

// GX default matrices / clear params (rodata/bss blobs)
extern float DAT_8058e3cc;     // default model/view matrix
extern float DAT_8058e40c;     // default projection matrix
extern float DAT_8058fc10, DAT_8058fc14, DAT_8058fc18, DAT_8058fc1c; // ortho clear params
extern float DAT_8058fc28, DAT_8058fc2c, DAT_8058fc30, DAT_8058fc34; // persp clear params
extern float DAT_8058fc38, DAT_8058fc3c;                              // shared near/far
extern float DAT_8058e44c;     // env-tex reset matrix
extern float DAT_8058e458, DAT_8058e468, DAT_8058e478;                // env-tex reset cells
extern int   DAT_8058e48c;     // sentinel "default material" record

// ---- GX / matrix runtime helpers (raw addresses; not yet decompiled) ----
extern void  FUN_803c88b0(int a, int b, int c, int d, unsigned int e, int f, int g); // set TEV combine
extern void  FUN_803c878c(int slot, unsigned int* konst);                            // set TEV konst colour
extern void  FUN_803cb1c8(float* mtx, int slot);                                     // load model/view matrix
extern void  FUN_803cb248(float* mtx, int slot);                                     // load texture matrix
extern void  FUN_803cb098(void* mtx, unsigned int slot);                             // load projection matrix
extern void  FUN_803cb468(double a, double b, double c, double d, double e, double f);// set clear/proj params
extern void  FUN_803cb2cc(float* mtx, int slot, int flag);                           // load tex-gen matrix
extern void  FUN_803cad80(unsigned int mode);                                        // set z-mode
extern void  FUN_803ca238(int slot, unsigned int* color);                            // set TEV konst reg
extern void  FUN_803ca1d4(int slot, unsigned int* color);                            // set TEV reg (RGBA)
extern void  FUN_803ca29c(int stage, int sel);                                       // set konst selector
extern void  FUN_803ca2f8(int stage, int mode);                                      // set alpha compare
extern void  FUN_803ca688(unsigned int cullMode);                                    // set cull mode
extern void  FUN_803cab98(int a, int b, int c, int d);                               // set blend mode
extern void  FUN_803c6aac(unsigned int zmode);                                       // set z-compare
extern void  FUN_803c8b84(unsigned int* texObj, unsigned int data, int w, int h,     // init tex object
                          int fmt, int a, int b, char c);
extern void  FUN_803c9288(void* texObj, unsigned int slot);                          // bind texture
extern void  FUN_803c8f8c(void* texObj, int a, int b);                               // set tex wrap
extern void  FUN_803c9314(int a, int b);                                             // set tex LOD

extern void  FUN_803a1248(double sx, double sy, double sz, float* outMtx);           // scale matrix
extern void  FUN_803a1214(double a, double b, double c, float* outMtx);              // translate matrix
extern void  FUN_803a1270(double a, double b, double c, float* inMtx, float* outMtx);// scale+bias matrix
extern void  FUN_803a1148(float* a, float* b, float* out);                           // matrix multiply
extern void  FUN_803a10e8(float* outMtx);                                            // identity matrix
extern void  FUN_803a1114(float* src, float* dst);                                   // copy 3x4 matrix

extern void  FUN_80181cf0(unsigned int stage, int a, int b, int c);                  // TEV colour input
extern void  FUN_80181e58(unsigned int stage, int a, int b, int c, int d);           // TEV colour op
extern void  FUN_80181d50(unsigned int stage, int a, int b, int c, int d, int e);    // TEV colour bias
extern void  FUN_80181dd4(unsigned int stage, int a, int b, int c, int d, int e);    // TEV alpha bias
extern void  FUN_80181ec8(unsigned int stage, int a, int b, int c, int d);           // TEV alpha op
extern void  FUN_80181f38(int stage, int a, int b, int c);                           // TEV order (2 tex)
extern void  FUN_80181fb4(unsigned int stage, int a, int b, int c, int d, int e);    // TEV order (ext)

extern void  FUN_8016e57c(void);                                                     // reset indirect stage
// batch_451 functions defined at file tail but called earlier in this TU:
void gx_material_commit(void);        // 0x80172664 (was FUN_80172664)
void gx_bind_material_fallback(void); // 0x801720a8 (was FUN_801720a8)
extern void  FUN_8017c540(void);                                                     // enter 2D/ortho mode
extern void  FUN_8017c5c0(void);                                                     // leave 2D/ortho mode
extern void  FUN_8017c4cc(float* out);                                               // get 2D transform
// batch_453 functions defined at file tail but called earlier in this TU:
void gx_set_tev_konst_color(unsigned int color);                                     // 0x8017402c
void gx_projtex_add_point(double a, double b, double c, double d, double e, double f, float g); // 0x801735cc
void gx_trail_add_point(unsigned int frameArgs, double b, double c, double d, double e, double f); // 0x80173da4
void gx_projtex_flush(void);                                                         // 0x80173520
void gx_trail_flush(void);                                                           // 0x80173fa0
extern void  FUN_801f88b0(void);                                                     // sprite texture bind prep

// batch_443 material record globals reused here
// (DAT_80610a20 texture-object table base already declared above)
// --- batch_444 render-state getter/setter state + shadow-proj rodata ---
extern unsigned char DAT_80612289;   // shadow-enable flag (set/return-prev)
extern float         FLOAT_8061228c; // shadow-intensity (set/return-prev)
extern unsigned char DAT_80612322;   // proj-tex-enable flag (set/return-prev)
extern int           DAT_806109c4;   // proj-tex source X (int, magic-double convert)
extern int           DAT_806109c8;   // proj-tex source Z (int, magic-double convert)
extern float         DAT_8058e47c, DAT_8058e480, DAT_8058e484, DAT_8058e488; // proj-tex matrix cells
extern int           DAT_8058fbf0;   // shadow/proj texture object
extern float         FLOAT_80616540;  // 255.0
extern double        DOUBLE_80616530; // int->double magic bias 0x4330000080000000

// --- batch_445: TEV indirect-stage packed-nibble table + setters ---
extern unsigned char DAT_8040f0b0[];  // per-stage packed nibble table (lo=A sel, hi=B sel)
extern void  FUN_803ca354(int idx, unsigned int oldSel, unsigned int newSel); // apply indirect-stage sel
extern unsigned long long FUN_803826ec(void);   // spawn-frame ptr (hi=index, lo=value)
extern void  FUN_80382738(void);                 // spawn-frame pop

extern unsigned long long FUN_803826f0(void);   // spawn-frame ptr (hi=rec, bit-flags in lo/hi)
extern void  FUN_8038273c(void);
extern unsigned long long FUN_803826dc(void);    // spawn-frame index-pair ptr
extern void  FUN_80382728(void);
extern unsigned long long FUN_803826d0(void);    // spawn-frame material-flags ptr
extern void  FUN_8038271c(void);

// named helpers not yet in generated_decls
extern void  render_entity_resolve_position(int out, int recB);   // resolve sub-object world pos


// gx_set_blend_mode_tagged (0x8016c594)
// Spawn-frame callback: program the GX blend/TEV combine from a bit-packed mode
// tag (high nibble = category 0..7, low nibble = coverage mask width).
void gx_set_blend_mode_tagged(void)
{
    unsigned int mask;
    unsigned int tag;
    unsigned int cat;
    unsigned int konst[6];

    tag = (unsigned int)FUN_803826f0();
    cat = tag >> 4;
    if (cat < 4) {
        if (cat == 0) {
            FUN_803c88b0(4, 0, 0, 1, 0, 0, 0);
        } else if (cat == 1) {
            FUN_803c88b0(4, 0, 0, 0, 0, 0, 0);
        } else if (cat != 3) {
            FUN_803c88b0(4, 1, 1, 0, 0, 0, 2);
        }
    } else {
        konst[0] = DAT_806164f4;
        mask = (1 << (tag & 0xf)) - 1;
        FUN_803c878c(4, konst);
        FUN_803c88b0(2, 0, 0, 0, 0, 0, 2);
        if (cat == 4) {
            FUN_803c88b0(0, 1, 0, 0, mask, 2, 2);
        } else {
            FUN_803c88b0(0, 1, 0, 0, mask, 2, 1);
        }
    }
    DAT_80612278 = (unsigned char)tag;
    FUN_8038273c();
}

// gx_render_state_flush (0x8016c6e8)
// Per-frame commit of any pending GX state: 2D/ortho toggle, model/view +
// projection matrices, clear/near-far params, full reflush trigger, fog, z-mode,
// and (if the blend tag changed) re-run gx_set_blend_mode_tagged.
void gx_render_state_flush(void)
{
    unsigned int fog;
    unsigned int fogNear;
    unsigned int fogColor;
    unsigned int fogFlag;

    if (DAT_80612323 == '\0') {
        if (DAT_80612318 != DAT_80612319) {
            if (DAT_80612319 == '\0') {
                FUN_8017c5c0();
            } else {
                FUN_8017c540();
            }
        }
        if (DAT_80612318 == '\0') {
            if (DAT_80612250 != 0) {
                FUN_803cb1c8(&DAT_8058e3cc, 0);
                if (DAT_8061225c == 0) {
                    FUN_803cb248(&DAT_8058e3cc, 0);
                }
                DAT_80612250 = 0;
            }
            if (DAT_80612310 != 0) {
                FUN_803cb098(&DAT_8058e40c, (unsigned int)DAT_80612321);
                DAT_80612310 = 0;
            }
            if (DAT_80612314 != 0) {
                FUN_803cb468((double)DAT_8058fc28, (double)DAT_8058fc2c,
                             (double)(DAT_8058fc30 - FLOAT_806164fc),
                             (double)(DAT_8058fc34 - FLOAT_806164fc),
                             (double)DAT_8058fc38, (double)DAT_8058fc3c);
                DAT_80612314 = 0;
            }
        } else if (DAT_80612314 != 0) {
            FUN_803cb468((double)DAT_8058fc10, (double)DAT_8058fc14,
                         (double)DAT_8058fc18, (double)DAT_8058fc1c,
                         (double)DAT_8058fc38, (double)DAT_8058fc3c);
            DAT_80612314 = 0;
        }

        if ((DAT_806109d0 != DAT_806109d0) /* material id sync placeholder */
            || (DAT_806109dc != DAT_806109d8) || (DAT_80612262 != '\0')) {
            gx_material_commit();
        }

        fog = DAT_80610a04;
        if (DAT_80610a04 != DAT_80610a08) {
            fogNear  = DAT_80610a04 & 0xff;
            fogColor = (DAT_80610a04 >> 8) & 0xff;
            fogFlag  = ((DAT_80610a04 >> 0x10) & 1) ^ 1;
            if (fogNear < 0x40) {
                FUN_80181f38(0, fogFlag, fogColor, fogNear);
            } else {
                FUN_80181fb4(0, fogFlag, fogColor, 0x3c, (DAT_80610a04 >> 0x11) & 1, fogNear);
            }
            DAT_80610a08 = fog;
        }
        if (DAT_806109e0 != DAT_806109e1) {
            FUN_803cad80((unsigned int)DAT_806109e0);
            DAT_806109e1 = DAT_806109e0;
        }
    }
    if (DAT_80612278 != DAT_80612277) {
        gx_set_blend_mode_tagged();
    }
}

// gx_setup_env_tex_matrices (0x8016c8fc)
// Build camera env-map / projected texture matrices (post-scale/translate) and
// load them into tex-gen slots 0x79, 0x76 and 0x6d.  Called each frame from the
// game tick (dispatch_5690).
void gx_setup_env_tex_matrices(void)
{
    double s;
    float scaleMtx[12];
    float baseMtx[12];
    float outMtx[14];

    FUN_803a1248((double)FLOAT_80616520, (double)FLOAT_80616524, (double)FLOAT_806164f8, scaleMtx);
    FUN_803a1214((double)FLOAT_80616514, (double)FLOAT_80616514, (double)FLOAT_806164fc, baseMtx);
    FUN_803a1148(baseMtx, scaleMtx, outMtx);
    FUN_803cb2cc(outMtx, 0x79, 0);

    s = (double)FLOAT_80616528;
    FUN_803a1248(s, s, s, scaleMtx);
    FUN_803a1148(outMtx, scaleMtx, outMtx);
    FUN_803cb2cc(outMtx, 0x76, 0);

    FUN_803a1248((double)FLOAT_80616514, (double)FLOAT_80616514, (double)FLOAT_806164f8, scaleMtx);
    FUN_803a1148(baseMtx, scaleMtx, outMtx);
    FUN_803cb2cc(outMtx, 0x6d, 0);
}

// gx_material_render_glow (0x8016c9c8)
// If the sub-object has a glow registry texture (glow_registry_lookup), bind the
// 32x24 glow RT texture, set up its TEV stage and (unless flagged 0x100) blend it
// with the base texture / mask colour.  Advances the caller's stage/slot counters.
void gx_material_render_glow(unsigned int a, unsigned int b, unsigned int* stageCtr,
                             int* rec, int matRec)
{
    unsigned int* stageP;
    unsigned int glowTex;
    unsigned int color;
    unsigned int* slotP;
    int envSel;
    unsigned int stage;
    unsigned int slot;
    unsigned int texSlot;
    unsigned int stage2;
    unsigned long long frame;
    unsigned int packed;
    unsigned char texObj[32];
    float pos[24];

    (void)a; (void)b;
    frame = FUN_803826dc();
    stageP = (unsigned int*)(unsigned int)(frame >> 0x20);
    slotP  = (unsigned int*)(unsigned int)frame;

    glowTex = (unsigned int)glow_registry_lookup(*rec);
    if (glowTex != 0) {
        stage = *stageP;  *stageP = stage + 1;
        slot  = *slotP;   *slotP  = slot + 1;
        texSlot = *stageCtr; *stageCtr = texSlot + 1;

        FUN_803c8b84((unsigned int*)texObj, glowTex, 0x20, 0x18, 4, 0, 0, '\0');
        FUN_803c9288(texObj, slot);
        render_entity_resolve_position((int)pos, rec[1]);
        if (FLOAT_806164fc == pos[0]) {
            envSel = 0x79;
        } else {
            envSel = 0x76;
        }
        FUN_80181fb4(texSlot, 0, 1, 0x30, 0, envSel);

        if (DAT_80612288 != '\0') {
            DAT_8058e458 = FLOAT_806164f8;
            DAT_8058e468 = FLOAT_806164f8;
            DAT_8058e478 = FLOAT_806164f8;
            FUN_803cb2cc(&DAT_8058e44c, 0x30, 0);
            DAT_80612288 = '\0';
        }

        if ((*(unsigned int*)(matRec + 0x1c) & 0x100) == 0) {
            stage2 = *stageP;  *stageP = stage2 + 1;
            slot   = *slotP;   *slotP  = slot + 1;
            FUN_803c9288((void*)(*(int*)DAT_80610a20 +
                          *(int*)(*(int*)(rec[1] + 0x10) + *(int*)(matRec + 8) * 0x10) * 0x58),
                         slot);
            FUN_80181cf0(stage, texSlot, glowTex, 0xff);
            FUN_80181e58(stage, 0xf, 0xf, 0xf, 8);
            FUN_80181d50(stage, 0, 0, 0, 1, 2);
            FUN_80181dd4(stage, 0, 0, 0, 1, 0);
            FUN_80181ec8(stage, 7, 7, 7, 0);
            FUN_80181cf0(stage2, 0, slot, 0xff);
            FUN_80181e58(stage2, 0xf, 4, 8, 0);
            FUN_80181d50(stage2, 0, 0, 0, 1, 0);
            FUN_80181ec8(stage2, 7, 7, 7, 0);
            FUN_80181dd4(stage2, 0, 0, 0, 1, 0);
        } else {
            unsigned int tint = *(unsigned int*)(matRec + 8);
            color = 0xff000000 | (((tint >> 8) & 0xff) << 16) | 0x0000ff00 | (tint & 0xff);
            packed = color;
            FUN_803ca238(1, &packed);
            FUN_803ca29c(stage, 0xd);
            FUN_80181cf0(stage, texSlot, glowTex, 0xff);
            FUN_80181e58(stage, 0xf, 0xe, 8, 0);
            FUN_80181d50(stage, 0, 0, 0, 1, 0);
            FUN_80181ec8(stage, 7, 7, 7, 0);
            FUN_80181dd4(stage, 0, 0, 0, 1, 0);
        }
    }
    FUN_80382728();
}

// gx_build_proj_tex_matrix (0x8016ccfc)
// Build the projected-texture matrix for tex-gen slot 0x33 from either the
// identity 2D transform (DAT_80612318 == 1) or the active model/view + proj
// matrices, applying a scale/bias, and load it.
void gx_build_proj_tex_matrix(void)
{
    double one;
    float outMtx[12];
    float projPart[2];
    float projA;
    float projRow[3];
    float scaleBias;
    float mv[2];
    float mvRow[3];
    float baseMtx[14];

    if (DAT_80612318 == '\x01') {
        FUN_803a10e8(baseMtx);
        FUN_8017c4cc(mv);
    } else {
        FUN_803a1114(&DAT_8058e3cc, baseMtx);
        mtx4_copy(&DAT_8058e40c, mv);
    }
    FUN_803a10e8(projPart);
    one = (double)FLOAT_806164fc;
    projRow[1] = -mvRow[1];
    projA = (float)(one + (double)mv[2]);   /* mvRow via mv layout */
    projRow[0] = (float)(one + (double)mvRow[2]);
    projPart[0] = mv[0];
    scaleBias = FLOAT_80616518;
    (void)projA; (void)scaleBias; (void)projRow;
    FUN_803a1270((double)FLOAT_80616510, (double)FLOAT_80616510, one, baseMtx, baseMtx);
    FUN_803a1148(projPart, baseMtx, outMtx);
    FUN_803cb2cc(outMtx, 0x33, 0);
}

// gx_indirect_stage_toggle (0x8016cdcc)
// Toggle the active indirect-texture stage: reset the previous one (and, if a
// stage was already active, reset again) and record the new stage index.
void gx_indirect_stage_toggle(int stage)
{
    if (DAT_80610a00 != stage) {
        FUN_8016e57c();
        if (DAT_80610a00 != -1) {
            FUN_8016e57c();
        }
        DAT_80610a00 = (char)stage;
    }
}

// gx_material_setup_tev (0x8016ce28)
// Main per-material TEV stage programming. Runs optional pre/override callbacks
// (DAT_80612284/7c), then binds the active material's base + optional secondary
// texture and programs the TEV colour/alpha stages for the material type
// (byte at DAT_80612254+0x4a), including dynamic-shadow, glow (bit flag), and
// second-texture-set paths. Returns the number of z-compare stages used via
// DAT_806109f9 side-effect and the base TEV stage count as its result.
unsigned int gx_material_setup_tev(int frameFlagsLo, int rec, int matRec)
{
    unsigned char konstA;
    unsigned int matBit;
    unsigned int* tex2;
    unsigned int stageCount;
    unsigned int cmpCount;
    unsigned int texCount;
    unsigned int stageArg;
    unsigned int matType;
    unsigned int argA;
    unsigned int argB;
    unsigned long long frame;
    unsigned int konst[6];  // local_a4/a0/9c pack + counters via local_94[]
    unsigned int stage;
    unsigned int cmp;
    unsigned int texSlot;
    long long dup[4];

    (void)frameFlagsLo;
    frame = FUN_803826d0();
    stageCount = 0;
    texCount   = 0;
    stageArg   = 1;
    cmpCount   = 0;

    if (DAT_80612284 != (int (*)(int, unsigned int*, unsigned int*))0) {
        stageCount = DAT_80612284(0, &cmpCount, &texCount);
    }
    stage = stageCount;

    if (DAT_8061227c != (int (*)(int, unsigned int*, unsigned int*))0) {
        stageCount = DAT_8061227c(stageCount, &cmpCount, &texCount);
        goto POST;
    }

    if ((frame & 0x100000000ULL) == 0) {
        stageCount = stageCount + 1;
        FUN_80181cf0(stage, 0xff, 0x100, 4);
        FUN_803ca29c(stage, 2);
        FUN_80181e58(stage, 0xb, 0xe, 10, 10);
        FUN_80181d50(stage, 0xe, 0, 0, 1, 0);
        FUN_80181ec8(stage, 7, 7, 7, 5);
        FUN_80181dd4(stage, 0, 0, 1, 1, 0);
        goto POST;
    }

    stageCount = stageCount + 1;

    if (((int*)DAT_80612254 == &DAT_8058e48c) && (DAT_806109dc != 0xe) && (DAT_806109dc != 0x18)) {
        FUN_80181cf0(stage, 0xff, 0xff, 4);
        FUN_80181e58(stage, 0xf, 0xb, 10, 0xf);
        FUN_80181ec8(stage, 7, 5, 7, 7);
        FUN_80181d50(stage, 0, 0, 2, 1, 0);
        FUN_80181dd4(stage, 0, 0, 1, 1, 0);
        goto POST;
    }
    if ((int*)DAT_80612254 == &DAT_8058e48c) {
        FUN_80181cf0(stage, 0xff, 0xff, 4);
        FUN_80181e58(stage, 0xf, 0xc, 10, 0xf);
        FUN_80181ec8(stage, 7, 5, 7, 7);
        FUN_80181d50(stage, 0, 0, 1, 1, 0);
        FUN_80181dd4(stage, 0, 0, 1, 1, 0);
        goto POST;
    }

    matType = (unsigned int)*(unsigned char*)(DAT_80612254 + 0x4a);
    cmpCount = cmpCount + 1;
    if (*(char*)(DAT_80612254 + 0x4a) == '\x0e') {
        FUN_803c9314(DAT_80612254 + 8, 0);
    }
    {
        unsigned int t = texCount; texCount = texCount + 1;
        FUN_803c9288((void*)DAT_80612254, t);
    }
    if ((frame & 0x200000000ULL) != 0) { cmpCount = cmpCount + 1; }
    if ((frame & 0x400000000ULL) != 0) { cmpCount = cmpCount + 2; }

    texSlot = (unsigned int)DAT_80610a0c;
    if ((frame & 0x400000000ULL) == 0) {
        if ((DAT_80612261 != '\0') && (FLOAT_806164f8 < FLOAT_806123d0)) {
            int shadowByte = (int)(FLOAT_80616538 * FLOAT_806123d0);
            konstA = (unsigned char)shadowByte;
            konst[1] = (konstA << 24) | (konstA << 16) | (konstA << 8) | konstA; // local_a4
            konst[2] = konst[1];                                                  // local_a0
            dup[0] = (long long)shadowByte;
            dup[1] = dup[0]; dup[2] = dup[0]; dup[3] = dup[0];
            FUN_803ca238(3, &konst[1]);
            FUN_803ca29c(stage, 0xf);
            FUN_803ca2f8(stage, 0x1f);
            FUN_80181cf0(stage, 0xff, 0xff, 4);
            FUN_80181e58(stage, 10, 0xf, 0xe, 0xf);
            FUN_80181ec8(stage, 5, 7, 6, 7);
            FUN_80181d50(stage, 0, 0, 0, 1, 0);
            FUN_80181dd4(stage, 0, 0, 0, 1, 0);
            stage = stageCount; stageCount = stageCount + 1;
            FUN_803ca29c(stage, 0xf);
            FUN_803ca2f8(stage, 0x1f);
            FUN_80181cf0(stage, 0xff, 0xff, 5);
            FUN_80181e58(stage, 0xf, 10, 0xe, 0);
            FUN_80181ec8(stage, 7, 5, 6, 0);
            FUN_80181d50(stage, 0, 0, 0, 1, 0);
            FUN_80181dd4(stage, 0, 0, 0, 1, 0);
            argB = 0; argA = 0;
            stage = stageCount; stageCount = stageCount + 1;
            goto STAGE_MAIN;
        }
        argB = 10; argA = 5;
    } else {
        FUN_80181f38(1, 10, 0x13, 0x3c);
        FUN_80181f38(2, 10, 0x14, 0x3c);
        FUN_80181cf0(stage, 1, 7, 0xff);
        FUN_80181e58(stage, 0xf, 0xf, 0xf, 8);
        FUN_80181ec8(stage, 7, 7, 7, 7);
        FUN_80181d50(stage, 0, 0, 0, 1, 0);
        FUN_80181dd4(stage, 0, 0, 0, 1, 0);
        stageArg = stageCount; stageCount = stageCount + 1;
        FUN_803ca2f8(stageArg, 0);
        FUN_80181cf0(stageArg, 2, 7, 0xff);
        FUN_80181e58(stageArg, 0xf, 8, 0xc, 0);
        FUN_80181ec8(stageArg, 7, 4, 6, 0);
        FUN_80181d50(stageArg, 0, 0, 0, 1, 0);
        FUN_80181dd4(stageArg, 0, 0, 0, 1, 0);
        argB = 0; argA = 0;
        stageArg = 3;
        stage = stageCount; stageCount = stageCount + 1;
    }

STAGE_MAIN:
    FUN_80181cf0(stage, 0, 0, 4);
    matBit = 1 << matType;
    if ((matBit & 0x3800) == 0) {
        FUN_80181e58(stage, 0xf, argB, 8, texSlot);
        FUN_80181d50(stage, 0, 0, 1, 1, 0);
        if ((matType != 3) && (matType != 7)) {
            if ((matBit & 0x610) == 0) {
                FUN_80181ec8(stage, 7, argA, 4, 7);
                FUN_80181dd4(stage, 0, 0, 1, 1, 0);
            } else {
                FUN_80181ec8(stage, 7, 7, 7, argA);
                FUN_80181dd4(stage, 0, 0, 0, 1, 0);
            }
        } else {
            FUN_80181ec8(stage, 7, 7, 7, argA);
            FUN_80181dd4(stage, 0, 0, 1, 1, 0);
        }
    } else {
        konst[0] = ((unsigned int*)DAT_80612254)[0x15];
        FUN_803ca238(0, &konst[0]);
        FUN_803ca29c(stage, 0xc);
        FUN_80181e58(stage, 0xf, argB, 0xe, texSlot);
        FUN_80181d50(stage, 0, 0, 1, 1, 0);
        FUN_80181ec8(stage, 7, argA, 4, 7);
        FUN_80181dd4(stage, 0, 0, 1, 1, 0);
        if (matType == 0xd) {
            gx_indirect_stage_toggle(stage);
        }
    }

    cmp = texCount;
    if ((matBit & 0x610) != 0) {
        texCount = texCount + 1;
        FUN_803c9288((void*)(DAT_80612254 + 8), cmp);
        {
            unsigned int s2 = stageCount; stageCount = stageCount + 1;
            FUN_80181cf0(s2, 0, cmp, 0xff);
            FUN_80181e58(s2, 0xf, 0xf, 0xf, 0);
            FUN_80181ec8(s2, 7, 0, 4, 7);
            FUN_80181d50(s2, 0, 0, 0, 1, 0);
            FUN_80181dd4(s2, 0, 0, 1, 1, 0);
            if (matType == 10) {
                gx_indirect_stage_toggle(s2);
            }
        }
    }

    if ((frame & 0x800000000ULL) != 0) {
        gx_material_render_glow(stageCount, cmpCount, &texCount, (int*)(unsigned int)frame, matRec);
    }
    if ((frame & 0x200000000ULL) != 0) {
        float projMtx[8];
        tex2 = (unsigned int*)(*(int*)DAT_80610a20 +
                *(int*)(*(int*)(((int*)(unsigned int)frame)[1] + 0x10) +
                        *(int*)(matRec + 0xc) * 0x10) * 0x58);
        FUN_80181f38(stageArg, 1, 4, 0x36);
        projMtx[0] = FLOAT_8061653c; projMtx[1] = FLOAT_806164f8;
        projMtx[2] = FLOAT_806164f8; projMtx[3] = FLOAT_806164f8;
        projMtx[4] = FLOAT_806164f8; projMtx[5] = FLOAT_8061653c;
        projMtx[6] = FLOAT_806164f8; projMtx[7] = FLOAT_806164f8;
        FUN_803cb2cc(projMtx, 0x36, 1);
        FUN_803c8f8c(tex2, 2, 2);
        FUN_803c9288(tex2, texCount);
        konst[1] = 0xffc0ffc0; konst[2] = 0xffc0ffc0;
        konst[3] = 0xffc0ffc0; konst[4] = 0xffc0ffc0;
        FUN_803ca1d4(3, &konst[1]);
        FUN_803ca2f8(stageCount, 4);
        FUN_80181cf0(stageCount, stageArg, texCount, 0xff);
        FUN_80181e58(stageCount, 8, 0, 0xd, 6);
        FUN_80181ec8(stageCount, 4, 0, 6, 3);
        FUN_80181d50(stageCount, 0, 0, 1, 1, 0);
        FUN_80181dd4(stageCount, 0, 0, 1, 1, 0);
        stageCount = stageCount + 1;
        texCount = texCount + 1;
    }

POST:
    if (DAT_80612280 != (int (*)(int, unsigned int*, unsigned int*))0) {
        stageCount = DAT_80612280(stageCount, &cmpCount, &texCount);
    }
    if (cmpCount != (unsigned int)DAT_806109f9) {
        FUN_803c6aac(cmpCount & 0xff);
        DAT_806109f9 = (unsigned char)cmpCount;
    }
    FUN_8038271c();
    return stageCount;
}

// gx_material_blend_by_type (0x8016d868)
// Program the GX blend mode (FUN_803cab98) for a material type id, with a few
// types also needing an extra TEV pass (returns the possibly-incremented stage).
unsigned int gx_material_blend_by_type(unsigned int stage, int type)
{
    if (type == 0xd) {
        FUN_803cab98(3, 1, 1, 0);
    } else if (type < 0xd) {
        if (type == 6) {
            FUN_803cab98(1, 6, 7, 0);
        } else if (type < 6) {
            if (type == 2) {
                FUN_803cab98(1, 4, 1, 0);
            } else if (type < 2) {
                if (type == 0) {
                    FUN_803cab98(1, 4, 0, 0);
                } else if (-1 < type) {
                    FUN_803cab98(1, 4, 5, 0);
                }
            } else if (type == 4) {
                FUN_803cab98(1, 5, 1, 0);
            } else if (type < 4) {
                FUN_80181cf0(stage, 0xff, 0xff, 0xff);
                FUN_80181e58(stage, 0xf, 1, 0, 0xf);
                FUN_80181ec8(stage, 7, 7, 7, 0);
                FUN_80181d50(stage, 0, 0, 0, 1, 0);
                FUN_80181dd4(stage, 0, 0, 0, 1, 0);
                FUN_803cab98(3, 1, 1, 0);
                DAT_80612264 = 1;
                stage = stage + 1;
            } else {
                FUN_803cab98(1, 1, 4, 0);
            }
        } else if (type == 10) {
            FUN_80181e58(stage - 1, 0xf, 0xf, 0xf, 0xc);
            FUN_80181d50(stage - 1, 0, 0, 0, 1, 0);
            DAT_80612264 = 1;
            FUN_803cab98(1, 2, 4, 0);
        } else if (type < 10) {
            if (type == 8) {
                FUN_803cab98(1, 6, 1, 0);
            } else if (type < 8) {
                FUN_803cab98(1, 7, 6, 0);
            }
        } else if (type < 0xc) {
            FUN_803cab98(1, 0, 5, 0);
        }
    } else if (type == 0x19) {
        FUN_803cab98(1, 0, 3, 0);
    } else if (type < 0x19) {
        if (type == 0x13) {
            FUN_803ca2f8(stage, 0);
            FUN_80181cf0(stage, 0xff, 0xff, 0xff);
            FUN_80181e58(stage, 0xf, 0xf, 0xf, 3);
            FUN_80181ec8(stage, 7, 7, 7, 6);
            FUN_80181d50(stage, 0, 0, 1, 1, 0);
            FUN_80181dd4(stage, 0, 0, 1, 1, 0);
            FUN_803cab98(1, 2, 1, 0);
            DAT_80612264 = 1;
            stage = stage + 1;
        } else if (type < 0x13) {
            if (type == 0x10) {
                FUN_80181ec8(stage - 1, 7, 7, 7, 1);
                FUN_80181dd4(stage - 1, 0, 0, 0, 1, 0);
                FUN_803cab98(1, 4, 1, 0);
                DAT_80612264 = 1;
            } else if (type < 0x10) {
                if (type < 0xf) {
                    FUN_803cab98(0, 0, 0, 0);
                } else {
                    FUN_80181ec8(stage - 1, 7, 7, 7, 1);
                    FUN_80181dd4(stage - 1, 0, 0, 0, 1, 0);
                    FUN_803cab98(1, 4, 5, 0);
                    DAT_80612264 = 1;
                }
            }
        } else if (0x17 < type) {
            FUN_803cab98(1, 1, 1, 0);
        }
    } else if (type == 0x1c) {
        FUN_803cab98(2, 0, 0, 1);
    } else if (type < 0x1c) {
        if (type < 0x1b) {
            FUN_803cab98(1, 0, 4, 0);
        }
    } else if (type == 999) {
        FUN_80181cf0(stage, 0xff, 0xff, 0xff);
        FUN_80181e58(stage, 0xf, 0, 1, 0xf);
        FUN_80181ec8(stage, 7, 7, 7, 0);
        FUN_80181d50(stage, 0, 0, 0, 1, 0);
        FUN_80181dd4(stage, 0, 0, 0, 1, 0);
        FUN_803cab98(1, 6, 5, 0);
        DAT_80612264 = 1;
        stage = stage + 1;
    }
    return stage;
}

// gx_material_blend_by_type_b (0x8016ddc0)
// Simplified blend-mode variant used for the default/sentinel material record.
unsigned int gx_material_blend_by_type_b(unsigned int stage, int type)
{
    switch (type) {
    case 0:    FUN_803cab98(1, 2, 0, 0); break;
    case 1:    FUN_803cab98(1, 2, 5, 0); break;
    case 2:    FUN_803cab98(1, 2, 1, 0); break;
    case 0xe:  FUN_803cab98(1, 2, 0, 0); break;
    case 0x18: FUN_803cab98(1, 2, 1, 0); break;
    }
    return stage;
}

// gx_material_apply (0x8016de80)
// Top-level material-apply entry (spawn-frame callback). Resolves the material
// via gx_material_setup_tev, chooses the blend-mode setup (sentinel material ->
// _b variant, else full), tracks indirect-stage cleanup and commits the cull
// mode when it changes.
void gx_material_apply(unsigned int a, unsigned int b, int type, unsigned int matRec, int rec)
{
    unsigned int clzA;
    unsigned int clzB;
    unsigned int recHi;
    unsigned int result;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826f0();
    recHi = (unsigned int)(frame >> 0x20);
    if (recHi == 0) goto DONE;

    result = 0xffffffff;
    if (type == 0x13) {
        if ((frame & 0x200000000ULL) != 0) {
            DAT_806109f8 = '\0';
            result = gx_material_blend_by_type(0, 0x13);
        }
    } else if (recHi != 0) {
        if ((frame & 0x100000000ULL) == 0) {
            result = (unsigned int)DAT_806109f8;
        } else {
            result = gx_material_setup_tev((int)frame, (int)matRec, rec);
            if (DAT_80612264 == '\0') {
                clzA = countLeadingZeros((int)&DAT_8058e48c - (int)DAT_80612254);
                clzB = countLeadingZeros((int)&DAT_8058e48c - (int)DAT_806109d0);
                if (clzA >> 5 != clzB >> 5) { recHi = recHi | 2; }
            } else {
                recHi = recHi | 2;
            }
            DAT_806109f8 = (unsigned char)result;
        }
        if ((recHi & 2) != 0) {
            DAT_80612264 = '\0';
            if ((int*)DAT_80612254 == &DAT_8058e48c) {
                result = gx_material_blend_by_type_b(result, type);
            } else {
                result = gx_material_blend_by_type(result, type);
            }
        }
        if ((-1 < DAT_80610a00) &&
            ((DAT_80612254 == 0) ||
             ((*(char*)(DAT_80612254 + 0x4a) != '\n') && (*(char*)(DAT_80612254 + 0x4a) != '\r')))) {
            FUN_8016e57c();
            DAT_80610a00 = -1;
        }
    }

    if ((int)DAT_80612263 != (int)result) {
        FUN_803ca688(result & 0xff);
        DAT_80612263 = (char)result;
    }
DONE:
    FUN_8038273c();
}


// ===========================================================================
// batch_444: render-state getter/setters + shadow/projected-texture helpers
//   gx_set_shadow_enable       0x8016e000  set shadow-enable flag, return prev
//   gx_set_shadow_intensity    0x8016e010  set shadow intensity, return prev
//   gx_set_zmode               0x8016e020  set z-mode byte, return prev
//   gx_build_shadow_proj_matrix 0x8016e034 build AABB shadow-proj matrix -> slot 0x21
//   gx_set_projtex_enable      0x8016e178  set proj-tex-enable flag, return prev
//   gx_set_shadow_mode         0x8016e188  set shadow/proj mode (wrapper)
//   gx_set_fog_range           0x8016e1a8  set fog/proj range (wrapper)
//   gx_set_proj_params         0x8016e1d4  set projection params (wrapper)
//   gx_material_projtex_stage  0x8016e1f8  spawn-frame cb: projected-tex TEV stage
//   gx_material_sprite_stage   0x8016e348  spawn-frame cb: sprite (0x362) TEV stage
// ===========================================================================

// mtx4_set_translation is declared in generated_decls as (float* mtx, f,f,f) but
// the shadow-proj call site uses the 3-double implicit-current-matrix form.
typedef void (*Mtx4SetXlate3Fn)(double, double, double);
#define MTX4_SET_XLATE3 ((Mtx4SetXlate3Fn)mtx4_set_translation)

// gx_set_shadow_enable (0x8016e000)
unsigned char gx_set_shadow_enable(unsigned char enable)
{
    unsigned char prev = DAT_80612289;
    DAT_80612289 = enable;
    return prev;
}

// gx_set_shadow_intensity (0x8016e010)
double gx_set_shadow_intensity(double intensity)
{
    double prev = (double)FLOAT_8061228c;
    FLOAT_8061228c = (float)intensity;
    return prev;
}

// gx_set_zmode (0x8016e020)
int gx_set_zmode(unsigned char zmode)
{
    char prev = (char)DAT_806109e0;
    DAT_806109e0 = zmode;
    return (int)prev;
}

// gx_build_shadow_proj_matrix (0x8016e034)
// Build a projected-texture matrix that maps an AABB (min at bounds[0..2],
// max at bounds[3..5]) into [0,1] texture space, centred, and load it into
// tex-gen slot 0x21 with a matching TEV order.  Used by weather portal
// precipitation for the ground shadow/splash projection.
void gx_build_shadow_proj_matrix(float* bounds)
{
    float projMtx[16];
    float composed[16];
    float scaleMtx[5];
    float scaleY;
    float biasMtx[16];   // local_88.. identity + centre translate

    MTX4_SET_XLATE3(-(double)((bounds[0] + bounds[3]) * FLOAT_80616514),
                    -(double)((bounds[1] + bounds[4]) * FLOAT_80616514),
                    -(double)((bounds[2] + bounds[5]) * FLOAT_80616514));

    mtx4_set_identity((int)scaleMtx);
    scaleMtx[0] = FLOAT_80616540 / (FLOAT_80616514 * (bounds[3] - bounds[0]));  // 255/width -> local_48
    scaleY      = FLOAT_80616540 / (FLOAT_80616514 * (bounds[5] - bounds[2]));  // 255/depth -> local_24
    (void)scaleY;

    mtx4_set_identity((int)biasMtx);
    biasMtx[0] = FLOAT_80616514;
    mtx4_write_translation((double)FLOAT_80616514, (double)FLOAT_80616514,
                           (double)FLOAT_806164fc, (int)biasMtx);

    mtx4_multiply(composed, scaleMtx, projMtx);
    mtx4_multiply(projMtx, biasMtx, projMtx);
    mtx4_transpose(projMtx, projMtx);
    FUN_803cb2cc(projMtx, 0x21, 0);
    FUN_80181f38(1, 0, 0, 0x21);
}

// gx_set_projtex_enable (0x8016e178)
unsigned char gx_set_projtex_enable(unsigned char enable)
{
    unsigned char prev = DAT_80612322;
    DAT_80612322 = enable;
    return prev;
}

// gx_set_shadow_mode (0x8016e188)
void gx_set_shadow_mode(unsigned int mode)
{
    gx_set_tev_konst_color(mode);
}

// gx_set_fog_range (0x8016e1a8)
void gx_set_fog_range(double a, double b, double c, double d, double e)
{
    gx_projtex_add_point(a, b, c, d, e, (double)FLOAT_806164f8, 3.3961514e+38f);
}

// gx_set_proj_params (0x8016e1d4)
void gx_set_proj_params(unsigned int a, double b, double c, double d, double e, double f)
{
    gx_trail_add_point(a, b, c, d, e, f);
}

// gx_material_projtex_stage (0x8016e1f8)
// Spawn-frame TEV-stage callback: build a projected-texture matrix from a world
// source position (DAT_806109c4/c8 via the int->double magic bias), bind the
// shadow/proj texture (DAT_8058fbf0) and program its blend TEV stage (slot 0x33).
void gx_material_projtex_stage(unsigned int a, unsigned int b, unsigned int* slotCtr)
{
    double bias;
    unsigned int stage;
    int texIdx;
    unsigned int slot;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826f0();
    bias = DOUBLE_80616530;
    stage = (unsigned int)(frame >> 0x20);
    slot = *slotCtr;  *slotCtr = slot + 1;

    DAT_8058e47c = FLOAT_806164f8;
    texIdx = *(int*)(unsigned int)frame;
    *(int*)(unsigned int)frame = texIdx + 1;
    DAT_8058e480 = (float)((double)(long long)((unsigned int)DAT_806109c8 ^ 0x80000000U) - bias);
    DAT_8058e488 = DAT_8058e47c;
    DAT_8058e484 = (float)((double)(long long)((unsigned int)DAT_806109c4 ^ 0x80000000U) - bias);

    gx_build_proj_tex_matrix();
    FUN_80181f38(texIdx, 0, 0, 0x33);
    FUN_803c9288(&DAT_8058fbf0, slot);
    FUN_80181cf0(stage, 1, slot, 4);
    FUN_80181e58(stage, 0xf, 10, 8, 0xf);
    FUN_80181d50(stage, 0, 0, 1, 1, 0);
    FUN_80181ec8(stage, 7, 7, 7, 0);
    FUN_80181dd4(stage, 0, 0, 0, 1, 0);
    FUN_8038273c();
}

// gx_material_sprite_stage (0x8016e348)
// Spawn-frame TEV-stage callback: bind sprite texture 0x362 and program a
// modulate TEV stage.
void gx_material_sprite_stage(unsigned int a, unsigned int b, unsigned int* slotCtr)
{
    unsigned int stage;
    int texIdx;
    unsigned int slot;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826f0();
    stage = (unsigned int)(frame >> 0x20);
    slot = *slotCtr;  *slotCtr = slot + 1;
    *(int*)(unsigned int)frame = *(int*)(unsigned int)frame + 1;

    texIdx = (int)sprite_get_texture_id(0x362);
    FUN_801f88b0();
    FUN_803c9288((void*)(*(int*)DAT_80610a20 + texIdx * 0x58), slot);
    FUN_80181cf0(stage, 1, slot, 0xff);
    FUN_80181e58(stage, 0xf, 0xf, 0xf, 0);
    FUN_80181d50(stage, 0, 0, 0, 1, 0);
    FUN_80181ec8(stage, 7, 0, 4, 7);
    FUN_80181dd4(stage, 0, 0, 0, 1, 0);
    FUN_8038273c();
}


// ===========================================================================
// batch_445: blend-tag / callback setters + TEV indirect-stage table updates
//   gx_set_blend_tag          0x8016e444  set blend-mode tag (cat<<4|mask)
//   gx_set_env_tex_matrix     0x8016e460  set env-tex reset matrix + flag
//   gx_set_material_override_cb 0x8016e490 set material override callback
//   gx_set_material_post_cb   0x8016e4b4  set material post-stage callback
//   gx_set_material_pre_cb    0x8016e4d8  set material pre-stage callback
//   gx_set_konst_alpha_sel    0x8016e4fc  set TEV konst-alpha selector
//   gx_mark_material_dirty    0x8016e51c  force material re-flush
//   gx_indirect_stage_set     0x8016e528  spawn-frame: set indirect-stage nibbles
//   gx_indirect_stage_set_hi  0x8016e57c  spawn-frame: set stage B (high nibble)
//   gx_indirect_stage_set_lo  0x8016e5e4  spawn-frame: set stage A (low nibble)
// ===========================================================================

// gx_set_blend_tag (0x8016e444)
// Store the packed blend-mode tag (category<<4 | mask) into the pending slot;
// category 3 applies immediately by also updating the current tag.
void gx_set_blend_tag(int category, unsigned char mask)
{
    DAT_80612277 = (unsigned char)((category << 4)) | mask;
    if (category != 3) {
        return;
    }
    DAT_80612278 = DAT_80612277;
}

// gx_set_env_tex_matrix (0x8016e460)
// Copy a 3x4 matrix into the env-tex reset matrix (DAT_8058e44c) and raise the
// reset flag so the next glow material render re-loads it.
void gx_set_env_tex_matrix(float* mtx)
{
    DAT_80612288 = 1;
    FUN_803a1114(mtx, &DAT_8058e44c);
}

// gx_set_material_override_cb (0x8016e490)
// Set the material override callback (DAT_8061227c); mark dirty on change.
// Returns the previous callback.
int gx_set_material_override_cb(int cb)
{
    int prev = (int)(long)DAT_8061227c;
    if ((int)(long)DAT_8061227c != cb) {
        DAT_80612262 = 1;
    }
    DAT_8061227c = (int (*)(int, unsigned int*, unsigned int*))(long)cb;
    return prev;
}

// gx_set_material_post_cb (0x8016e4b4)
int gx_set_material_post_cb(int cb)
{
    int prev = (int)(long)DAT_80612280;
    if ((int)(long)DAT_80612280 != cb) {
        DAT_80612262 = 1;
    }
    DAT_80612280 = (int (*)(int, unsigned int*, unsigned int*))(long)cb;
    return prev;
}

// gx_set_material_pre_cb (0x8016e4d8)
int gx_set_material_pre_cb(int cb)
{
    int prev = (int)(long)DAT_80612284;
    if ((int)(long)DAT_80612284 != cb) {
        DAT_80612262 = 1;
    }
    DAT_80612284 = (int (*)(int, unsigned int*, unsigned int*))(long)cb;
    return prev;
}

// gx_set_konst_alpha_sel (0x8016e4fc)
// Set the TEV konst-alpha selector (DAT_80610a0c); mark dirty on change.
// Returns the previous selector.
unsigned int gx_set_konst_alpha_sel(unsigned int sel)
{
    unsigned int prev = (unsigned int)DAT_80610a0c;
    DAT_80610a0c = (unsigned char)sel;
    if (sel != prev) {
        DAT_80612262 = 1;
    }
    return prev;
}

// gx_mark_material_dirty (0x8016e51c)
void gx_mark_material_dirty(void)
{
    DAT_80612262 = 1;
}

// gx_indirect_stage_set (0x8016e528)
// Spawn-frame cb: for indirect-stage `idx` (hi word) set the combined nibble
// entry (lo A-sel | selB<<4) in DAT_8040f0b0, calling FUN_803ca354 on change.
void gx_indirect_stage_set(unsigned int a, unsigned int b, unsigned int selB)
{
    int idx;
    unsigned int combined;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826f0();
    idx = (int)(frame >> 0x20);
    combined = ((unsigned int)frame) | (selB << 4);
    if ((unsigned char)DAT_8040f0b0[idx] != combined) {
        FUN_803ca354(idx, (unsigned int)frame, selB);
        DAT_8040f0b0[idx] = (unsigned char)combined;
    }
    FUN_8038273c();
}

// gx_indirect_stage_set_hi (0x8016e57c)
// Spawn-frame cb: set the high nibble (stage B selector) of indirect-stage
// table entry `idx`, preserving the low nibble.  Called by gx_indirect_stage_toggle,
// gx_material_apply and weather overlays.
void gx_indirect_stage_set_hi(void)
{
    int idx;
    unsigned int newSel;
    unsigned int loNibble;
    unsigned long long frame;

    frame = FUN_803826ec();
    idx = (int)(frame >> 0x20);
    newSel = (unsigned int)frame;
    if (newSel != ((unsigned int)(unsigned char)DAT_8040f0b0[idx] >> 4)) {
        loNibble = (unsigned char)DAT_8040f0b0[idx] & 0xf;
        FUN_803ca354(idx, loNibble, newSel);
        DAT_8040f0b0[idx] = (unsigned char)(loNibble | (newSel << 4));
    }
    FUN_80382738();
}

// gx_indirect_stage_set_lo (0x8016e5e4)
// Spawn-frame cb: set the low nibble (stage A selector) of indirect-stage table
// entry `idx`, preserving the high nibble.
void gx_indirect_stage_set_lo(void)
{
    int idx;
    unsigned int newSel;
    unsigned int hiNibble;
    unsigned long long frame;

    frame = FUN_803826ec();
    idx = (int)(frame >> 0x20);
    newSel = (unsigned int)frame;
    if (newSel != ((unsigned char)DAT_8040f0b0[idx] & 0xf)) {
        hiNibble = (unsigned int)(unsigned char)DAT_8040f0b0[idx] >> 4;
        FUN_803ca354(idx, newSel, hiNibble);
        DAT_8040f0b0[idx] = (unsigned char)(newSel | (hiNibble << 4));
    }
    FUN_80382738();
}


// ===========================================================================
// batch_446: env-tex scale + active-material select + blend-mode dispatch
//   gx_set_env_tex_scale       0x8016e648  set env-tex scale FLOAT_80612294, if changed
//   gx_indirect_matrix_update  0x8016e694  spawn-frame: set indirect matrix table entry
//   gx_get_env_reflect_matrix  0x8016e71c  lazily build+cache env-map reflect matrix
//   gx_set_active_material     0x8016e77c  set active material by index (DAT_80612254)
//   gx_set_active_material_b   0x8016e7e4  identical alt entry of gx_set_active_material
//   gx_material_state_nop      0x8016e84c  empty stub
//   gx_set_cull_mode           0x8016e850  set cull-mode byte DAT_80610a0d, return prev
//   gx_get_blend_mode          0x8016e8a4  get current blend-mode index
//   gx_set_blend_mode          0x8016e8ac  set blend-mode by index (14-case GX switch)
//   gx_set_blend_func_custom   0x8016eb88  spawn-frame: program custom blend/alpha funcs
// ===========================================================================

// ---- batch_446 render-state globals ----
extern float         FLOAT_80612294;   // current env-tex scale (change detect)
extern unsigned int  DAT_8040f048[];   // indirect-matrix table (per-index u32)
extern unsigned char DAT_8058e4e4[];   // indirect-matrix mode table (per-index byte)
extern float         DAT_8058e500;     // cached env-map reflect matrix (transpose result)
extern unsigned char DAT_80612258;     // env-map reflect matrix built flag
extern unsigned char DAT_80610a0d;     // current cull mode (change detect)
extern int           DAT_806109d4;     // current blend-mode index (change detect)

// ---- batch_446 GX runtime helpers (raw addresses) ----
extern void  FUN_803cb4d4(double a, double b);                 // set env-tex scale (2-arg)
extern void  FUN_803c67d8(int idx, unsigned int mtx, unsigned int mode); // set indirect matrix
extern void  FUN_803cac78(unsigned int cullMode);              // program GX cull mode
extern void  FUN_803cac44(int enable, int src, int op);        // program GX blend equation
extern void  FUN_803ca41c(int srcFac, int dstFac, int a, int b, int c); // program GX blend factors
extern unsigned long long FUN_803826e8(void);                  // spawn-frame ptr (hi=idx, lo=mtx)
extern void  FUN_80382734(void);                               // spawn-frame pop (pair of 803826e8)
extern unsigned long long __save_gpr_28(void);                 // reg-pair spill (hi=cat, lo=val)
extern void  __restore_gpr_28(void);                           // reg-pair restore

// gx_set_env_tex_scale (0x8016e648)
// Set the env-texture scale factor (FLOAT_80612294) via FUN_803cb4d4 only when it
// changes from the currently-applied value.
void gx_set_env_tex_scale(double scale)
{
    if (scale != (double)FLOAT_80612294) {
        FUN_803cb4d4((double)FLOAT_806164fc, scale);
        FLOAT_80612294 = (float)scale;
    }
}

// gx_indirect_matrix_update (0x8016e694)
// Spawn-frame cb: for indirect-matrix slot `idx` (hi word) set both the matrix
// value (lo word) and the mode byte (param mode); reprogram GX (FUN_803c67d8)
// only when either changed.
void gx_indirect_matrix_update(unsigned int a, unsigned int b, unsigned int mode)
{
    int idx;
    unsigned int mtx;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e8();
    idx = (int)(frame >> 0x20);
    mtx = (unsigned int)frame;
    if ((mtx != DAT_8040f048[idx]) || ((mode & 0xff) != (unsigned int)DAT_8058e4e4[idx])) {
        FUN_803c67d8(idx, mtx, mode);
        DAT_8040f048[idx] = mtx;
        DAT_8058e4e4[idx] = (unsigned char)mode;
    }
    FUN_80382734();
}

// gx_get_env_reflect_matrix (0x8016e71c)
// Lazily build the env-map reflect matrix = transpose(projMtx * viewMtx) once
// (DAT_80612258 latch) and return a pointer to it.
float* gx_get_env_reflect_matrix(void)
{
    if (DAT_80612258 == '\0') {
        mtx4_multiply(&DAT_8058e40c, &DAT_8058e3cc, &DAT_8058e500);
        mtx4_transpose(&DAT_8058e500, &DAT_8058e500);
        DAT_80612258 = '\x01';
    }
    return &DAT_8058e500;
}

// gx_set_active_material (0x8016e77c)
// Set the active material record pointer (DAT_80612254) from a material index
// into the texture-object table (DAT_80610a20 base, 0x58 stride).  Out-of-range
// or below the low-water index falls back to FUN_801720a8.  Referenced ~337
// times across the render pipeline.
void gx_set_active_material(int index)
{
    if ((index < 0) || (((int*)DAT_80610a20)[3] <= index) || (index <= ((int*)DAT_80610a20)[2])) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = *(int*)DAT_80610a20 + index * 0x58;
    }
}

// gx_set_active_material_b (0x8016e7e4)
// Byte-identical alternate entry of gx_set_active_material (distinct call site).
void gx_set_active_material_b(int index)
{
    if ((index < 0) || (((int*)DAT_80610a20)[3] <= index) || (index <= ((int*)DAT_80610a20)[2])) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = *(int*)DAT_80610a20 + index * 0x58;
    }
}

// gx_material_state_nop (0x8016e84c)
void gx_material_state_nop(void)
{
}

// gx_set_cull_mode (0x8016e850)
// Set the current GX cull mode byte (DAT_80610a0d) via FUN_803cac78 when it
// changes; return the previous mode.
unsigned int gx_set_cull_mode(unsigned int mode)
{
    unsigned int prev = (unsigned int)DAT_80610a0d;
    if (mode != prev) {
        FUN_803cac78(mode & 0xff);
        DAT_80610a0d = (unsigned char)mode;
    }
    return prev;
}

// gx_get_blend_mode (0x8016e8a4)
int gx_get_blend_mode(void)
{
    return DAT_806109d4;
}

// gx_set_blend_mode (0x8016e8ac)
// Top-level blend-mode setter (~385 xrefs): for a blend-mode index 0..0xd program
// the GX cull enable (gx_set_cull_mode), blend equation (FUN_803cac44) and blend
// factors (FUN_803ca41c).  Index -1 or unchanged is a no-op.  Returns previous.
int gx_set_blend_mode(int index)
{
    int prev = DAT_806109d4;

    if ((index != DAT_806109d4) && (index != -1)) {
        switch (index) {
        case 0:
            gx_set_cull_mode(1);  FUN_803cac44(0, 7, 0);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 1:
            gx_set_cull_mode(1);  FUN_803cac44(1, 7, 1);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 2:
            gx_set_cull_mode(1);  FUN_803cac44(1, 3, 0);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 3:
            gx_set_cull_mode(1);  FUN_803cac44(1, 4, 0);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 4:
            gx_set_cull_mode(1);  FUN_803cac44(1, 3, 1);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 5:
            gx_set_cull_mode(0);  FUN_803cac44(1, 3, 1);   FUN_803ca41c(4, 0x60, 0, 7, 0); break;
        case 6:
            gx_set_cull_mode(1);  FUN_803cac44(1, 3, 1);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 9:
            gx_set_cull_mode(1);  FUN_803cac44(1, 4, 1);   FUN_803ca41c(7, 0, 0, 7, 0); break;
        case 10:
            gx_set_cull_mode(0);  FUN_803cac44(1, 3, 1);   FUN_803ca41c(4, 0, 0, 7, 0); break;
        case 0xb:
            gx_set_cull_mode(1);  FUN_803cac44(1, 3, 0);   FUN_803ca41c(4, 0, 0, 7, 0); break;
        case 0xc:
            gx_set_cull_mode(0);  FUN_803cac44(0, 3, 0);   FUN_803ca41c(4, 0, 0, 7, 0); break;
        case 0xd:
            gx_set_cull_mode(0);  FUN_803cac44(1, 3, 0);   FUN_803ca41c(3, 0x60, 0, 7, 0); break;
        default:
            break;
        }
        DAT_806109d4 = index;
    }
    return prev;
}

// gx_set_blend_func_custom (0x8016eb88)
// Spawn-frame cb: program a fully custom blend/alpha configuration.  The frame
// register pair (FUN/__save_gpr_28) carries a blend-factor category (hi) and a
// factor value (lo); param3 controls cull mode, param5/param6 select an alpha
// blend equation.  Invalidates the cached blend-mode index (DAT_806109d4=-1).
void gx_set_blend_func_custom(unsigned int a, unsigned int b, int cullSel,
                              unsigned int d, unsigned int alphaEq, unsigned int alphaVal)
{
    int cat;
    unsigned int val;
    unsigned long long frame;

    (void)a; (void)b; (void)d;
    frame = __save_gpr_28();
    cat = (int)(frame >> 0x20);
    val = (unsigned int)frame;

    DAT_806109d4 = 0xffffffff;
    if (cat == 0) {
        gx_set_cull_mode(1);
    } else {
        switch (cat) {
        case 1: FUN_803ca41c(0, val & 0xff, 1, 0, 0); break;
        case 2: FUN_803ca41c(7, val & 0xff, 1, 0, 0); break;
        case 3: FUN_803ca41c(1, val & 0xff, 1, 0, 0); break;
        case 4: FUN_803ca41c(3, val & 0xff, 1, 0, 0); break;
        case 5: FUN_803ca41c(2, val & 0xff, 1, 0, 0); break;
        case 6: FUN_803ca41c(6, val & 0xff, 1, 0, 0); break;
        case 7: FUN_803ca41c(4, val & 0xff, 1, 0, 0); break;
        case 8: FUN_803ca41c(5, val & 0xff, 1, 0, 0); break;
        default: break;
        }
        if (cullSel == 0) {
            gx_set_cull_mode(0);
        } else if (cullSel == 2) {
            gx_set_cull_mode(1);
        }
    }

    switch (alphaEq) {
    case 0: FUN_803cac44(0, 7, alphaVal & 0xff); break;
    case 1: FUN_803cac44(1, 0, alphaVal & 0xff); break;
    case 2: FUN_803cac44(1, 3, alphaVal & 0xff); break;
    case 3: FUN_803cac44(1, 1, alphaVal & 0xff); break;
    case 4: FUN_803cac44(1, 6, alphaVal & 0xff); break;
    case 5: FUN_803cac44(1, 4, alphaVal & 0xff); break;
    case 6: FUN_803cac44(1, 2, alphaVal & 0xff); break;
    default: break;
    }
    __restore_gpr_28();
}


// ===========================================================================
// batch_447: fog / colour-write / colour-update render-state helpers
//   gx_apply_fog_mode          0x8016edac  apply fog config for mode (0=off), ret cur mode
//   gx_set_fog_color           0x8016ee1c  set fog-enable byte + fog colour
//   gx_set_color_write_mask    0x8016ee58  enable/disable colour-channel write mask
//   gx_set_color_update_mode   0x8016ee98  set colour-update mode (enum 0..3), ret prev
//   gx_get_color_update_packed 0x8016ef70  get packed colour-update state
//   gx_set_color_update_packed 0x8016ef78  spawn-frame: program alpha-cmp/colour-update
//   gx_material_state_nop_b    0x8016efe4  empty stub
//   gx_set_state_c0            0x8016efe8  set DAT_806122c0
//   gx_get_state_c0            0x8016eff0  get DAT_806122c0
//   gx_invalidate_state_e4     0x8016eff8  invalidate DAT_806109e4 (= -1)
// ===========================================================================

// ---- batch_447 render-state globals ----
extern unsigned char DAT_80612292;   // current fog mode (change detect)
extern unsigned char DAT_80612320;   // fog-enable byte
extern unsigned int  DAT_806109fc;   // packed colour-update state (hi=alpha-cmp, lo=colour-upd)
extern int           DAT_806122c0;   // misc render-state word (get/set)
extern int           DAT_806109e4;   // cached blend/state index (invalidate to -1)

// ---- batch_447 GX runtime helpers (raw addresses) ----
extern void  FUN_8017fd0c(double a, double b, double c, double d, double e, double f,
                          int g, int h, int i);            // set fog params (disable path)
extern void  water_surface_fog_apply(void);                // FUN_800f32a0 (already named)
extern void  FUN_803c7e28(unsigned char* color, unsigned int mode); // set fog colour
extern void  FUN_803cadac(int enable, unsigned int mask);  // set colour-channel write mask
extern void  FUN_803cac18(unsigned int enable);            // set alpha-compare enable
extern void  FUN_803cabec(unsigned int enable);            // set colour-update enable

// forward decl (ee98 calls ef78)
unsigned int gx_set_color_update_packed(unsigned int packed);

// gx_apply_fog_mode (0x8016edac)
// Apply the fog configuration for `mode` when it differs from the currently
// applied mode: mode 0 disables fog (FUN_8017fd0c all-zero), any other mode
// applies the level/water fog params (water_surface_fog_apply).  Returns the
// currently-applied mode (the caller records the new one).
unsigned int gx_apply_fog_mode(unsigned int mode)
{
    unsigned int cur = (unsigned int)DAT_80612292;
    if (mode != cur) {
        if (mode == 0) {
            double z = (double)FLOAT_806164f8;
            FUN_8017fd0c(z, z, z, z, z, z, 0, 0, 0);
        } else {
            water_surface_fog_apply();
        }
    }
    return cur;
}

// gx_set_fog_color (0x8016ee1c)
// Set the fog-enable byte (DAT_80612320); when enabling, program the fog colour
// (duplicated into a 2-word local as the RGBA source) via FUN_803c7e28.
void gx_set_fog_color(int enable, unsigned int color, unsigned int mode)
{
    unsigned int rgba[2];

    DAT_80612320 = (unsigned char)enable;
    if (enable != 0) {
        rgba[0] = color;
        rgba[1] = color;
        FUN_803c7e28((unsigned char*)rgba, mode);
    }
}

// gx_set_color_write_mask (0x8016ee58)
// Enable (non-zero) or disable (0) the colour-channel write mask (all channels).
void gx_set_color_write_mask(int enable)
{
    if (enable == 0) {
        FUN_803cadac(0, 0xff);
    } else {
        FUN_803cadac(1, 0xff);
    }
}

// gx_set_color_update_mode (0x8016ee98)
// Map a small colour-update mode enum (0..3) to the packed 32-bit alpha-compare /
// colour-update word, program it via gx_set_color_update_packed, then map the
// returned previous packed word back to the enum and return it.
//   enum 0 -> 0            (both off)
//   enum 1 -> 0xff000000   (alpha-compare on)
//   enum 2 -> 0x00ffffff   (colour-update on)
//   enum 3 -> 0xffffffff   (both on)
unsigned int gx_set_color_update_mode(int mode)
{
    unsigned int packed;
    unsigned int prevPacked;
    unsigned int result;

    if (mode == 2) {
        packed = 0x00ffffff;
    } else if (mode < 2) {
        if (mode == 0) {
            packed = 0;
        } else if (mode >= 0) {
            packed = 0xff000000;
        } else {
            packed = 0;
        }
    } else if (mode < 4) {
        packed = 0xffffffff;
    } else {
        packed = 0;
    }

    prevPacked = gx_set_color_update_packed(packed);

    result = 0;
    if (prevPacked == 0) {
        result = 2;
    } else if ((int)prevPacked < 0) {
        if (prevPacked == 0xff000000) {
            result = 1;
        } else if ((0xff000000 < prevPacked) && (prevPacked != 0xffffffff)) {
            result = 0;
        }
    } else if (prevPacked == 0x00ffffff) {
        result = 3;
    }
    return result;
}

// gx_get_color_update_packed (0x8016ef70)
unsigned int gx_get_color_update_packed(void)
{
    return DAT_806109fc;
}

// gx_set_color_update_packed (0x8016ef78)
// Spawn-frame worker: takes a packed alpha-compare(hi byte)/colour-update(lo 3
// bytes) word (retrieved from the frame register via FUN_803826f0 -> `packed`).
// On change, derives the two enable booleans via CLZ-on-masked-bytes and programs
// alpha-compare (FUN_803cac18) and colour-update (FUN_803cabec).  Returns the
// previous packed word.
unsigned int gx_set_color_update_packed(unsigned int packed)
{
    unsigned int next;
    unsigned int prev;
    unsigned int alphaEnable;
    unsigned int colorEnable;

    (void)FUN_803826f0();   // frame-slot fetch (arg arrives in r3 = `packed`)
    next = packed;
    prev = DAT_806109fc;
    if (next != DAT_806109fc) {
        alphaEnable = countLeadingZeros(next & 0xff000000) >> 5;
        colorEnable = countLeadingZeros(next & 0x00ffffff) >> 5;
        FUN_803cac18(alphaEnable & 0xff);
        FUN_803cabec(colorEnable & 0xff);
        DAT_806109fc = next;
    }
    FUN_8038273c();
    return prev;
}

// gx_material_state_nop_b (0x8016efe4)
void gx_material_state_nop_b(void)
{
}

// gx_set_state_c0 (0x8016efe8)
void gx_set_state_c0(int value)
{
    DAT_806122c0 = value;
}

// gx_get_state_c0 (0x8016eff0)
int gx_get_state_c0(void)
{
    return DAT_806122c0;
}

// gx_invalidate_state_e4 (0x8016eff8)
void gx_invalidate_state_e4(void)
{
    DAT_806109e4 = 0xffffffff;
}


// ===========================================================================
// batch_448: sub-material select + scissor rect + 2D fill-rect quad drawers
//   gx_get_sub_material_id     0x8016f004  get sub-material id (DAT_806109dc)
//   gx_set_sub_material_id     0x8016f00c  set sub-material id + const-alpha (637 xrefs)
//   gx_get_scissor_rect        0x8016f0a0  read current scissor rect (x0,y0,x1,y1)
//   gx_set_scissor_rect        0x8016f0e4  clamp+set scissor rect on change (135 xrefs)
//   gx_fill_rect_blend         0x8016f1c8  spawn-frame fill-rect (blend 1 + colour mode)
//   gx_fill_rect_flags         0x8016f24c  spawn-frame fill-rect (flags-driven colour mode)
//   gx_draw_fill_rect_i        0x8016f358  core 2D int-coord fill-rect quad (GX FIFO)
//   gx_fill_rect_clipped       0x8016f4bc  clipped 2D fill-rect vs clip rect (143 xrefs)
//   gx_draw_fill_rect_f        0x8016f6d0  float-coord quad wrapper (default depth)
//   gx_draw_fill_rect_f_ex     0x8016f6f4  float-coord quad drawer impl (GX FIFO)
// ===========================================================================

// ---- batch_448 render-state globals ----
// (sub-material id DAT_806109dc + screen dims DAT_806109c4/c8 already declared above)
extern int           DAT_806109e8;      // scissor x0
extern int           DAT_806109ec;      // scissor y0
extern int           DAT_806109f0;      // scissor x1
extern int           DAT_806109f4;      // scissor y1
extern int           DAT_80611748[];    // clip-rect base [x0,x1,_,_,_,y0,y1]
extern int           DAT_80611570;      // draw-stat counter (verts)
extern int           DAT_80611574;      // draw-stat counter
extern int           DAT_80611578;      // draw-stat counter

// ---- batch_448 rodata ----
extern float FLOAT_80616550;   // fill-rect UV/w const (int drawer)
extern float FLOAT_80616554;   // fill-rect UV/w const (clipped drawer)
extern float FLOAT_80616558;   // default fill-rect depth

// ---- batch_448 GX runtime helpers (raw addresses) ----
extern void  FUN_803ca174(int slot, unsigned int* color);   // set TEV const colour
extern void  FUN_803cb540(int x, int y, int w, int h);      // program GX scissor
extern void  FUN_8017c658(void);                            // enter 2D draw state
extern void  FUN_8017c474(int leave);                       // 2D draw begin(0)/end(1)
extern void  FUN_8017fab4(void);                            // set 2D vertex format
void         gx_begin_primitive(int vtxdesc, int fmt, int count); // 0x80172818 (was FUN_80172818), defined in batch_452 below
extern void  FUN_8017c670(void);                            // finish 2D draw state
extern unsigned long long FUN_803826e0(void);               // spawn-frame ptr (flags hi, args)
extern void  FUN_8038272c(void);                            // spawn-frame pop (of 803826e0)
extern unsigned long long FUN_803826e4(void);               // spawn-frame ptr (x0/y0 pair)
extern void  FUN_80382730(void);                            // spawn-frame pop (of 803826e4)
extern unsigned int FUN_80382650(void);                     // frame fetch: colour word (float drawer)
extern void  FUN_8038269c(void);                            // frame pop (of 80382650)

// forward decl (f1c8/f24c call f358)
void gx_draw_fill_rect_i(int x0, int y0, unsigned int x1, unsigned int y1, unsigned int color);
// forward decl (f6d0 calls f6f4)
void gx_draw_fill_rect_f_ex(unsigned int color, double x0, double x1, double y1, double y0v);

// GX FIFO write port (write-only volatile at 0xCC008000) + typed store macros
extern volatile float        DAT_cc008000;       // GX FIFO float write port
extern volatile unsigned int DAT_cc008000_u;     // GX FIFO u32 write port (same address)
#define GXF(v) (DAT_cc008000   = (float)(v))
#define GXU(v) (DAT_cc008000_u = (unsigned int)(v))

// int->double magic-bias dequant helper (matches DOUBLE_80616530 = 0x4330000080000000)
static inline float gx_i2f(unsigned int v)
{
    union { double d; unsigned int u[2]; } m;
    m.u[0] = 0x43300000u;
    m.u[1] = v ^ 0x80000000u;
    return (float)(m.d - DOUBLE_80616530);
}

// gx_get_sub_material_id (0x8016f004)
unsigned int gx_get_sub_material_id(void)
{
    return DAT_806109dc;
}

// gx_set_sub_material_id (0x8016f00c)
// Set the sub-material id (DAT_806109dc, ~637 xrefs).  For ids in [0xf,0x17] it
// derives a doubled/rounded alpha from DAT_806122c0 (caching it into
// DAT_806109e4) and programs a TEV constant colour (FUN_803ca174 slot 1).
// Returns the previous sub-material id.
unsigned int gx_set_sub_material_id(int id)
{
    unsigned int prev;
    unsigned int alpha;
    unsigned int color[2];

    prev = DAT_806109dc;
    DAT_806109dc = id;
    if ((0xe < id) && (id < 0x18)) {
        unsigned char a = 0xff;
        alpha = (DAT_806122c0 & 0xff) * 2 + ((DAT_806122c0 >> 6) & 1);
        DAT_806109e4 = DAT_806122c0;
        if (alpha < 0xff) {
            a = (unsigned char)alpha;
        }
        color[0] = ((unsigned int)DAT_806122c0 & 0xffffff00) | a;
        color[1] = color[0];
        FUN_803ca174(1, color);
    }
    return prev;
}

// gx_get_scissor_rect (0x8016f0a0)
void gx_get_scissor_rect(int* x0, int* y0, int* x1, int* y1)
{
    if (x0 != 0) { *x0 = DAT_806109e8; }
    if (y0 != 0) { *y0 = DAT_806109ec; }
    if (x1 != 0) { *x1 = DAT_806109f0; }
    if (y1 != 0) { *y1 = DAT_806109f4; }
}

// gx_set_scissor_rect (0x8016f0e4)
// Set the GX scissor rect, clamping (x0,y0)-(x1,y1) to the screen bounds
// (DAT_806109c4 width / DAT_806109c8 height); reprogram GX (FUN_803cb540) only
// when the rect changes.  x0/y0 arrive via the frame register pair. ~135 xrefs.
void gx_set_scissor_rect(unsigned int a, unsigned int b, int x1, int y1)
{
    int x0;
    int y0;
    unsigned long long frame;

    (void)a; (void)b;
    frame = __save_gpr_28();
    x0 = (int)(frame >> 0x20);
    y0 = (int)frame;
    if ((long long)frame < 0) { x0 = 0; }
    if (DAT_806109c4 < x1) { x1 = DAT_806109c4 - 1; }
    if (y0 < 0) { y0 = 0; }
    if (DAT_806109c8 < y1) { y1 = DAT_806109c8 - 1; }
    if (x1 < x0) { x0 = 0; x1 = DAT_806109c4 - 1; }
    if (y1 < y0) { y0 = 0; y1 = DAT_806109c8 - 1; }

    if ((DAT_806109e8 != x0) || (DAT_806109f0 != x1) ||
        (DAT_806109ec != y0) || (DAT_806109f4 != y1)) {
        FUN_803cb540(x0, y0, x1 - x0, y1 - y0);
        DAT_806109e8 = x0;
        DAT_806109ec = y0;
        DAT_806109f0 = x1;
        DAT_806109f4 = y1;
    }
    __restore_gpr_28();
}

// gx_fill_rect_blend (0x8016f1c8)
// Spawn-frame fill-rect: temporarily force blend mode 1 + colour-update mode 0,
// draw the int-coord fill rect (gx_draw_fill_rect_i) with (x0,y0) from the frame
// pair and (x1,y1)=param_3/param_4, then restore the previous blend/colour state.
void gx_fill_rect_blend(unsigned int a, unsigned int b, unsigned int x1, unsigned int y1)
{
    int x0;
    int y0;
    int prevBlend;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e8();
    x0 = (int)(frame >> 0x20);
    y0 = (int)frame;
    prevBlend = gx_set_blend_mode(1);
    gx_set_color_update_mode(0);
    gx_draw_fill_rect_i(x0, y0, x1, y1, 0);
    gx_set_blend_mode(prevBlend);
    gx_set_color_update_mode(0);   /* restore prev colour-update mode (return ignored) */
    FUN_80382734();
}

// gx_fill_rect_flags (0x8016f24c)
// Spawn-frame fill-rect with a flags word (frame hi dword) selecting the
// colour-update mode and whether to override the blend mode, then draw the
// int-coord fill rect.  Flags: bit 0x8 + bits 0x7 pick colour-update mode
// (3/1/0/2); bit 0x10 skips the blend override.
void gx_fill_rect_flags(unsigned int a, unsigned int b, unsigned int arg3,
                        unsigned int x1, unsigned int y1, unsigned int color)
{
    unsigned int flags;
    int x0;
    int prevColor;
    int prevBlend;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e0();
    flags = (unsigned int)(frame >> 0x20);
    x0 = (int)frame;   /* draw x0 (r4 in orig comes from frame args) */

    if ((flags & 0x8) && (flags & 0x7)) {
        prevColor = gx_set_color_update_mode(3);
    } else if (!(flags & 0x8) && (flags & 0x7)) {
        prevColor = gx_set_color_update_mode(1);
    } else if (!(flags & 0x8) && !(flags & 0x7)) {
        prevColor = gx_set_color_update_mode(0);
    } else {
        prevColor = gx_set_color_update_mode(2);
    }

    if ((flags & 0x10) == 0) {
        prevBlend = gx_set_blend_mode(0);
        gx_draw_fill_rect_i(x0, (int)arg3, x1, y1, color);
        gx_set_blend_mode(prevBlend);
    } else {
        gx_draw_fill_rect_i(x0, (int)arg3, x1, y1, color);
    }
    gx_set_color_update_mode(prevColor);
    FUN_8038272c();
}

// gx_draw_fill_rect_i (0x8016f358)
// Core 2D int-coordinate fill-rect: bind default material (gx_set_sub_material_id),
// enter 2D draw state, begin a 4-vertex quad and stream the corners to the GX
// FIFO (int->float via the magic-bias idiom).  (x0,y0) arrive via the frame pair;
// (x1,y1)=x1i/y1i; depth/color = color.
void gx_draw_fill_rect_i(int x0, int y0, unsigned int x1i, unsigned int y1i, unsigned int color)
{
    int prevMat;
    float fx0;
    float fy0;
    unsigned long long frame;

    (void)x0; (void)y0;
    frame = FUN_803826e4();
    prevMat = gx_set_sub_material_id(0);
    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    fx0 = gx_i2f((unsigned int)(frame >> 0x20));
    fy0 = gx_i2f((unsigned int)frame);

    GXF(fx0);            GXF(fy0);            GXF(FLOAT_80616550); GXU(color);
    GXF(gx_i2f(x1i));    GXF(fy0);            GXF(FLOAT_80616550); GXU(color);
    GXF(fx0);            GXF(gx_i2f(y1i));    GXF(FLOAT_80616550); GXU(color);
    GXF(gx_i2f(x1i));    GXF(gx_i2f(y1i));    GXF(FLOAT_80616550); GXU(color);

    FUN_8017c670();
    gx_set_sub_material_id(prevMat);
    FUN_8017c474(1);
    FUN_80382730();
}

// gx_fill_rect_clipped (0x8016f4bc)
// Clipped 2D fill-rect (~143 xrefs): sort the corners, intersect against the
// current clip rect (DAT_80611748 [x0,x1,..,y0,y1]); if any area remains, emit
// the quad to the GX FIFO with `color` and bump the draw-stat counters.  (x0,y0)
// arrive via the frame register pair.
void gx_fill_rect_clipped(unsigned int a, unsigned int b, int x1, int y1, unsigned int color)
{
    int x0;
    int y0;
    int lo;
    int hi;
    unsigned long long frame;

    (void)a; (void)b;
    frame = FUN_803826e8();
    x0 = (int)(frame >> 0x20);
    y0 = (int)frame;

    lo = x0;
    if (x1 < x0) { lo = x1; x1 = x0; }
    hi = y0;
    if (y1 < y0) { hi = y1; y1 = y0; }

    if (lo < DAT_80611748[0]) { lo = DAT_80611748[0]; }
    if ((lo <= DAT_80611748[1] + 1) && (DAT_80611748[0] <= x1)) {
        if (DAT_80611748[1] + 1 < x1) { x1 = DAT_80611748[1] + 1; }
        if (hi < DAT_80611748[5]) { hi = DAT_80611748[5]; }
        if ((hi <= DAT_80611748[6] + 1) && (DAT_80611748[5] <= y1)) {
            if (DAT_80611748[6] + 1 < y1) { y1 = DAT_80611748[6] + 1; }

            FUN_8017c658();
            FUN_8017c474(0);
            FUN_8017fab4();
            gx_begin_primitive(0x98, 0, 4);

            GXF(gx_i2f((unsigned int)lo)); GXF(gx_i2f((unsigned int)hi)); GXF(FLOAT_80616554); GXU(color);
            GXF(gx_i2f((unsigned int)x1)); GXF(gx_i2f((unsigned int)hi)); GXF(FLOAT_80616554); GXU(color);
            GXF(gx_i2f((unsigned int)lo)); GXF(gx_i2f((unsigned int)y1)); GXF(FLOAT_80616554); GXU(color);
            GXF(gx_i2f((unsigned int)x1)); GXF(gx_i2f((unsigned int)y1)); GXF(FLOAT_80616554); GXU(color);

            FUN_8017c670();
            FUN_8017c474(1);
            DAT_80611570 = DAT_80611570 + 2;
            DAT_80611574 = DAT_80611574 + 2;
            DAT_80611578 = DAT_80611578 + 2;
        }
    }
    FUN_80382734();
}

// gx_draw_fill_rect_f (0x8016f6d0)
// Float-coordinate fill-rect quad wrapper: forward to gx_draw_fill_rect_f_ex with
// the default depth (FLOAT_80616558).
void gx_draw_fill_rect_f(unsigned int color, double x0, double x1, double y1)
{
    gx_draw_fill_rect_f_ex(color, x0, x1, y1, (double)FLOAT_80616558);
}

// gx_draw_fill_rect_f_ex (0x8016f6f4)
// Float-coordinate fill-rect quad drawer: enter 2D state, begin a 4-vertex quad
// and stream float corners (x0/y0 .. x1/y1) at depth `depth` with the colour word
// fetched from the frame register (FUN_80382650); bump draw-stat counters.
void gx_draw_fill_rect_f_ex(unsigned int color, double x0, double x1, double y1, double depth)
{
    unsigned int colorWord;
    double y0;

    (void)color;
    colorWord = FUN_80382650();
    y0 = (double)FLOAT_80616558;   /* y0 arrives in f1 alongside the frame fetch */

    FUN_8017c658();
    FUN_8017c474(0);
    FUN_8017fab4();
    gx_begin_primitive(0x98, 0, 4);

    GXF((float)y0);   GXF((float)x0); GXF((float)depth); GXU(colorWord);
    GXF((float)x1);   GXF((float)x0); GXF((float)depth); GXU(colorWord);
    GXF((float)y0);   GXF((float)y1); GXF((float)depth); GXU(colorWord);
    GXF((float)x1);   GXF((float)y1); GXF((float)depth); GXU(colorWord);

    FUN_8017c670();
    FUN_8017c474(1);
    DAT_80611570 = DAT_80611570 + 2;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
    FUN_8038269c();
}


// ===========================================================================
// batch_451 : 0x80171fd8 - 0x801727b8
// GX material-select / texture-state layer.  Bind active material by index
// (bounds-checked), the big fallback resolver that builds shadow / sentinel /
// off-screen materials, the indirect-blend combine setup, the per-frame material
// commit (gx_material_commit -> gx_material_apply), and the env/reflect + tex
// transform param setters.  Companion to the batch_443-448 material layer above.
//
//   gx_set_reflect_matrix_b     0x80171fd8  reflect-matrix reset (proj flag = 1)
//   gx_bind_material_by_index   0x80172014  bounds-checked material select
//   gx_bind_material_by_index_w 0x80172088  wrapper
//   gx_bind_material_fallback   0x801720a8  shadow/sentinel/off-screen resolver
//   gx_set_alpha_write          0x801724b4  enable/disable colour-write (all channels)
//   gx_set_indirect_blend_mode  0x801724fc  indirect stage + blend combine (dual)
//   gx_set_indirect_blend_mode_b0x801725c0  indirect blend combine (single)
//   gx_material_commit_w        0x8017263c  wrapper
//   gx_material_commit          0x80172664  per-frame material apply
//   gx_set_tex_transform_params 0x801727b8  store 6 tex-transform floats + dirty
// ===========================================================================

// ---- batch_451 additional externs ----
extern int   DAT_80612260;     // active indirect blend mode A (change detect)
extern int   DAT_80612270;     // prev material record (change detect)
extern unsigned char DAT_80612275;  // colour-update override flag
// (DAT_80612261 shadow/overlay-enable, DAT_806109c4/c8 RT dims, DAT_80612289
//  shadow-enable, DAT_806109d0 applied-material id all declared above)

extern int   DAT_8058e48c;     // sentinel shadow material record
extern int   DAT_8058e540;     // off-screen RT material record
extern short DAT_8058e58a, DAT_8058e58c, DAT_8058e58e, DAT_8058e590, DAT_8058e592;
extern int   DAT_8058fbd0, DAT_8058fbb0, DAT_8058fb90, DAT_8058fbec, DAT_8058e53c;
extern short DAT_8058e4d6, DAT_8058e4d8, DAT_8058e4da, DAT_8058e4dc, DAT_8058e4de;
extern float DAT_8058fc28, DAT_8058fc2c, DAT_8058fc30, DAT_8058fc34, DAT_8058fc38, DAT_8058fc3c;

extern float FLOAT_80616560;   // shadow blur base coefficient
extern float FLOAT_8061228c;   // shadow intensity
// (FLOAT_80616518 already declared above)
extern float FLOAT_80616568;   // shadow blur radius scale
extern float FLOAT_8061656c;   // shadow blur X coefficient
extern float FLOAT_80616570;   // shadow blur Y coefficient
extern float FLOAT_80616574;   // shadow-off blur coefficient
extern float FLOAT_80616578;   // env-tex scale when material flag 0x4000

extern void  FUN_803c8fa8(int matRec, int a, int b);   // set material colour-write
extern void  FUN_803c7ea0(char a, unsigned char *b, char c, unsigned char *d); // program blur/copy filter
extern void  FUN_803c8874(int mode);                   // set TEV combine passes
extern unsigned int FUN_803c9040(int matRec);          // resolve RT texture handle
extern void  FUN_803c78c8(int a, int b, unsigned int w, unsigned int h); // set copy rect
extern void  FUN_803c7978(int a, int b, unsigned int c, int d);          // set copy scale
extern void  FUN_803c8224(unsigned int handle, int b); // bind RT texture
extern void  FUN_803c7080(void);                       // begin RT copy
extern void  FUN_803c94d8(void);                       // end RT copy
extern void  leaf_31e8(int dst, unsigned int src, int len); // memcpy
extern unsigned long long __save_gpr_25(void);         // spawn-frame reader (idx:subidx)
extern void  __restore_gpr_25(void);

// gx_set_reflect_matrix_b (0x80171fd8)
// Variant of gx_set_reflect_matrix (0x80171f9c): store transpose(matrix) into the
// env-tex reset matrix, clear the env-reflect latch, but leave BOTH the reflect
// dirty flag (DAT_80612310=1) and the projected-texture reset flag
// (DAT_80612321=1) set.  Caller: FUN_801912ac.
void gx_set_reflect_matrix_b(float *matrix)
{
    mtx4_transpose(matrix, (float *)&DAT_8058e40c);
    DAT_80612258 = 0;
    DAT_80612310 = 1;
    DAT_80612321 = 1;
}

// gx_bind_material_by_index (0x80172014)
// Bind the active material from a table index: negative or out-of-range indices
// fall back to gx_bind_material_fallback; otherwise DAT_80612254 = table base +
// index*0x58.  Callers: render_instance_apply_material, character_shield_effect_render.
void gx_bind_material_by_index(int index)
{
    if (index < 0) {
        gx_bind_material_fallback();
    } else if ((index < 0) || (((int*)DAT_80610a20)[3] <= index) || (index <= ((int*)DAT_80610a20)[2])) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = *(int*)DAT_80610a20 + index * 0x58;
    }
}

// gx_bind_material_by_index_w (0x80172088)
// Thin wrapper over gx_bind_material_by_index.
void gx_bind_material_by_index_w(int index)
{
    gx_bind_material_by_index(index);
}

// gx_bind_material_fallback (0x801720a8)
// Fallback active-material resolver used when a normal material index is out of
// range or when a sentinel material tag is requested.  Reads the material tag +
// sub-tag from the spawn frame (__save_gpr_25 -> idx:subidx).  Handles:
//   - a positive "border width" mode (in_r7 != 0) that programs a blurred copy
//     filter (FUN_803c7ea0),
//   - the shadow intensity path (FLOAT_8061228c) with the same filter,
//   - sentinel tag -5  -> off-screen RT material DAT_8058e540 (copies a 4-word
//     descriptor block from DAT_8058fbec),
//   - sentinel tags -7 / -9 range -> shadow/projected materials copied from the
//     DAT_8058fbxx descriptor tables, with a render-target copy pass
//     (FUN_803c9040 / 78c8 / 7978 / 8224 / 7080 / 94d8) sized to the RT.
// NOTE: in_r7 (the border-width count register) is an incoming register the
// decompiler could not source; it is modelled here as 0 (the common no-border
// path) so the structure stays faithful and compiles.  Referenced ~20 sites.
void gx_bind_material_fallback(void)
{
    unsigned long long framePair;
    int matTag, subTag;
    long long lPair;
    int savedZMode;
    unsigned char blurBuf[16];
    int in_r7 = 0;               /* border-width count register (see note) */
    void *descSrc = 0;
    unsigned int descLen = 0;
    unsigned char descMode = 0;

    framePair = __save_gpr_25();
    lPair = (long long)framePair;
    matTag = (int)((unsigned long long)framePair >> 0x20);   /* r25 */
    subTag = (int)framePair;                                 /* r26 */

    if ((matTag == -1) || (((int*)DAT_80610a20)[3] <= matTag) || (lPair < -0x800000000LL)) {
        DAT_80612254 = 0;
        __restore_gpr_25();
        return;
    }

    if (in_r7 == 0) {
        if (FLOAT_806164f8 < FLOAT_8061228c) {
            int bx = (int)(FLOAT_80616560 * FLOAT_8061228c);
            int by = (int)(FLOAT_80616574 * (FLOAT_806164fc - FLOAT_8061228c));
            blurBuf[0] = (unsigned char)bx;
            blurBuf[1] = (unsigned char)bx;
            blurBuf[2] = (unsigned char)by;
            blurBuf[3] = (unsigned char)by;
            blurBuf[4] = (unsigned char)by;
            blurBuf[5] = (unsigned char)by;
            blurBuf[6] = (unsigned char)by;
            blurBuf[7] = (unsigned char)('@' - (char)(bx << 2) - (char)(by << 1));
            FUN_803c7ea0(0, 0, 1, blurBuf);
        }
    } else {
        float t = gx_i2f((unsigned int)(in_r7 + 0x7f)) / FLOAT_80616568;
        int cx, cy;
        if ((t < FLOAT_806164f8) || (FLOAT_80616518 < t))
            t = FLOAT_80616518;
        cx = (int)(FLOAT_8061656c * t);
        cy = (int)(FLOAT_80616570 * t);
        blurBuf[0] = 0; blurBuf[1] = 0;
        blurBuf[2] = (unsigned char)cx;
        blurBuf[3] = (unsigned char)cx;
        blurBuf[4] = (unsigned char)cy;
        blurBuf[5] = 0;
        blurBuf[6] = 0;
        FUN_803c7ea0(0, 0, 1, blurBuf);
    }

    savedZMode = gx_set_zmode(0);

    if (matTag == -5) {
        /* off-screen RT material: copy a 4-entry descriptor block */
        int *src = (int *)&DAT_8058fbec;
        int *dst = (int *)&DAT_8058e53c;
        int n = 4;
        do {
            dst[1] = src[1];
            dst[2] = src[2];
            src += 2;
            dst += 2;
            n--;
        } while (n != 0);
        DAT_8058e58c = (short)((DAT_806109c4 >> 1) + ((DAT_806109c4 < 0 && (DAT_806109c4 & 1)) ? 1 : 0));
        DAT_8058e58e = (short)(DAT_8058e58c * 2);
        DAT_8058e58a = 1;
        DAT_8058e590 = 0x100;
        DAT_8058e592 = 0x200;
        DAT_80612254 = (int)&DAT_8058e540;
        if (DAT_806109d0 == (unsigned int)(int)&DAT_8058e540)
            DAT_806109d0 = 0xffffffff;
    } else if (lPair < -0x500000000LL) {
        if (matTag == -7) {
            descLen = (subTag != 4) ? 1u : 0x27u;
            descMode = 7;
            descSrc = &DAT_8058fb90;
        } else if (lPair > -0x700000001LL) {
            if (DAT_80612289 == 0) {
                /* build the shadow sentinel material */
                leaf_31e8((int)&DAT_8058e48c, (unsigned int)(int)&DAT_8058fbd0, 0x20);
                DAT_8058e4d8 = (short)DAT_806109c4;
                DAT_8058e4d6 = 1;
                DAT_8058e4dc = (short)DAT_806109c8;
                DAT_80612254 = (int)&DAT_8058e48c;
                DAT_8058e4da = DAT_8058e4d8;
                DAT_8058e4de = DAT_8058e4dc;
            } else if (subTag == 4) {
                descLen = 5; descMode = 6; descSrc = &DAT_8058fbb0;
            } else {
                descLen = 4; descSrc = &DAT_8058fbd0; descMode = 1;
            }
        }
    }

    if (descSrc != 0) {
        unsigned int w = (unsigned int)DAT_806109c4 + 0x1f;
        unsigned int h = (unsigned int)DAT_806109c8 + 0x1f;
        unsigned int handle = FUN_803c9040((int)descSrc);
        FUN_803c78c8(0, 0, w & 0xffe0, h & 0xffe0);
        FUN_803c7978((short)((DAT_806109c4 >> 1) + ((DAT_806109c4 < 0 && (DAT_806109c4 & 1)) ? 1 : 0)),
                     0x100, descLen, 1);
        FUN_803c8224(handle, 0);
        FUN_803c7080();
        FUN_803c94d8();
        leaf_31e8((int)&DAT_8058e540, (unsigned int)descSrc, 0x20);
        DAT_8058e590 = (short)((DAT_806109c8 >> 1) + ((DAT_806109c8 < 0 && (DAT_806109c8 & 1)) ? 1 : 0));
        DAT_8058e58c = (short)((DAT_806109c4 >> 1) + ((DAT_806109c4 < 0 && (DAT_806109c4 & 1)) ? 1 : 0));
        DAT_8058e58e = (short)(DAT_8058e58c * 2);
        DAT_8058e592 = (short)DAT_806109c8;
        DAT_80612254 = (int)&DAT_8058e540;
        DAT_8058e58a = (short)descMode;
        if (DAT_806109d0 == (unsigned int)(int)&DAT_8058e540)
            DAT_806109d0 = 0xffffffff;
    }

    gx_set_zmode((unsigned char)savedZMode);
    if ((in_r7 != 0) || (FLOAT_806164f8 < FLOAT_8061228c))
        FUN_803c7ea0(0, 0, 0, 0);
    __restore_gpr_25();
}

// gx_set_alpha_write (0x801724b4)
// Enable (param!=0) or disable (param==0) colour-write for the active material
// via FUN_803c8fa8.  Callers: character_nametag_render, hud_draw_fullscreen_flash.
void gx_set_alpha_write(int enable)
{
    if (enable == 0)
        FUN_803c8fa8(DAT_80612254, 0, 0);
    else
        FUN_803c8fa8(DAT_80612254, 1, 1);
}

// gx_set_indirect_blend_mode (0x801724fc)
// Program the indirect-texture stage + blend combine (dual pass).  On change:
// mode 0 resets the two indirect stage selectors and one blend pass; mode !=0
// programs a two-pass blend combine (FUN_803c88b0) + two passes.  Marks material
// dirty (DAT_80612262).  Callers: render_instance_draw + material setup.
void gx_set_indirect_blend_mode(unsigned int mode)
{
    if (DAT_80612260 != (int)mode) {
        if (mode == 0) {
            if (DAT_80610a00 != 0)  gx_indirect_stage_set_hi();
            if (DAT_80610a00 != 1)  gx_indirect_stage_set_hi();
            FUN_803c8874(1);
        } else {
            gx_indirect_stage_set_hi();
            gx_indirect_stage_set_hi();
            FUN_803c88b0(5, 0, 0, 1, 0, 2, 1);
            FUN_803c8874(2);
        }
        DAT_80612260 = (int)(unsigned char)mode;
        DAT_80612262 = 1;
    }
}

// gx_set_indirect_blend_mode_b (0x801725c0)
// Simpler single-selector variant of gx_set_indirect_blend_mode.
void gx_set_indirect_blend_mode_b(unsigned int mode)
{
    if (DAT_80612261 != (int)mode) {
        if (mode == 0) {
            FUN_803c8874(1);
        } else {
            FUN_803c88b0(5, 0, 0, 1, 0, 2, 1);
            FUN_803c8874(2);
        }
        DAT_80612261 = (int)(unsigned char)mode;
        DAT_80612262 = 1;
    }
}

// gx_material_commit_w (0x8017263c)
// Thin wrapper over gx_material_commit.
void gx_material_commit_w(void)
{
    gx_material_commit();
}

// gx_material_commit (0x80172664)
// Per-frame material commit: build the material-apply flag words from the current
// material record + sub-material id, program the env-tex scale, then hand off to
// gx_material_apply.  The record ptr arrives in the spawn frame (FUN_803826e8).
// Called from gx_render_state_flush and gx_material_commit_w.
void gx_material_commit(void)
{
    int subId = DAT_806109dc;
    int rec;
    unsigned int flagsA = 0, flagsB = 0;
    unsigned long long frame;

    frame = FUN_803826e8();
    rec = (int)frame;

    if (DAT_80612254 != 0) {
        flagsB = 1;
        if ((rec != 0) && (*(int *)(rec + 0xc) != -1))
            flagsB = 3;
        if (DAT_80612260 != 0)
            flagsB |= 4;
    }
    if ((rec != 0) && ((*(unsigned int *)(rec + 0x1c) & 0x80) != 0))
        flagsB |= 8;

    if ((rec == 0) || ((*(unsigned int *)(rec + 0x1c) & 0x4000) == 0))
        gx_set_env_tex_scale((double)FLOAT_806164f8);
    else
        gx_set_env_tex_scale((double)FLOAT_80616578);

    if (DAT_80612275 != 0)
        flagsB |= 0x10;

    if ((DAT_80612254 != (int)DAT_806109d0) || (DAT_80612270 != rec) || (DAT_80612262 != 0))
        flagsA = 1;
    if ((int)DAT_806109dc != (int)DAT_806109d8)
        flagsA |= 2;

    gx_material_apply(flagsA, flagsB, subId, (unsigned int)(frame >> 0x20), rec);

    DAT_80612262 = (DAT_806109dc == 0x13) ? 1 : 0;
    DAT_806109d8 = DAT_806109dc;
    DAT_806109d0 = (unsigned int)DAT_80612254;
    if (DAT_80612262)
        DAT_806109d8 = (unsigned int)-3;
    DAT_80612270 = rec;
    FUN_80382734();
}

// gx_set_tex_transform_params (0x801727b8)
// Store 6 texture-transform floats (offset/scale/rotation params) into the
// active tex-transform block DAT_8058fc28.. and mark it dirty (DAT_80612314).
// Callers: viewport_set_gx, gx_init_render_mode, FUN_801912ac.
void gx_set_tex_transform_params(double p0, double p1, double p2, double p3, double p4, double p5)
{
    DAT_8058fc28 = (float)p0;
    DAT_80612314 = 1;
    DAT_8058fc2c = (float)p1;
    DAT_8058fc30 = (float)p2;
    DAT_8058fc34 = (float)p3;
    DAT_8058fc38 = (float)p4;
    DAT_8058fc3c = (float)p5;
}


// ===========================================================================
// batch_452 : 0x801727e0 - 0x80172dc4
// GX primitive-begin / render-state-flush wrappers + depth-span batching.
// gx_begin_primitive is the shared "flush pending state then GXBegin" entry that
// every 2D/immediate drawer funnels through; the *_batch pair open/close a
// textured draw batch (blend tag, material bind, tex-scale, indirect stage);
// gx_batch_depth_range coalesces near/far depth spans that gx_end_batch commits.
// Companion to the batch_443-451 material layer above.
//
//   gx_force_state_flush        0x801727e0  force a full render-state reflush
//   gx_begin_primitive          0x80172818  flush + GXBegin (shared draw entry)
//   gx_load_pos_matrix_w        0x8017286c  wrapper
//   gx_load_pos_matrix          0x80172894  flush + load position matrix
//   gx_batch_depth_range        0x801728e0  coalesce depth spans (tex-gen remap)
//   gx_set_num_chans            0x80172a04  get/set GX channel-count (if changed)
//   gx_begin_textured_batch     0x80172a4c  open textured draw batch
//   gx_end_batch                0x80172c54  close draw batch, commit depth spans
//   gx_begin_batch_mode5        0x80172d88  wrapper (mode 5)
//   gx_begin_projected_batch    0x80172dc4  open projected/decal draw batch
// ===========================================================================

// ---- batch_452 additional externs ----
extern unsigned char DAT_80612274;   // depth-span count
extern unsigned char DAT_80612276;   // indirect-stage-lo restore flag
extern unsigned char DAT_8061226c;   // batch mode (0 none / 1 projected / 2 decal)
extern unsigned char DAT_806109fa;   // batch begin-mode (0xff none)
extern unsigned char DAT_806122b4;   // projected-tex active flag
extern unsigned char DAT_80612290;   // decal-suppress flag
// (DAT_80612322 decal blend-tag flag + DAT_80612323 decal/suppress-flush flag
//  both declared earlier in this file as unsigned char)
extern int   DAT_8061229c;           // decal scratch page ptr
extern int   DAT_80611d54;           // active camera/view record
extern int   DAT_8058e598;           // depth-span list: lo values (stride 8)
extern int   DAT_8058e59c;           // depth-span list: hi values (stride 8)
extern float FLOAT_806122a0, FLOAT_806122a4, FLOAT_806122a8, FLOAT_806122ac, FLOAT_806122b0;
extern float FLOAT_806122c4, FLOAT_806122c8;
extern float FLOAT_8061657c;         // projected-tex default scale

extern void  FUN_803c744c(unsigned char vtxdesc, unsigned char fmt, unsigned short count); // GXBegin
extern void  FUN_803cb028(unsigned int a, unsigned int b);   // GXLoadPosMtxImm
extern void  FUN_803c7694(unsigned int *out5);               // GXGet channel state
extern void  FUN_803c766c(unsigned int v);                   // GXSet channel state
extern void  FUN_8039955c(unsigned int lo, int len);         // commit depth span
extern void  FUN_803c6ae8(int a, int b);                     // set indirect tex-coord
// batch_454 functions defined at file tail but called earlier in this TU:
unsigned int gx_set_fog_params(unsigned int params);         // 0x801742a8 (was FUN_801742a8)
extern void  mem_page_free(int page);

// gx_force_state_flush (0x801727e0)
// Force a full render-state reflush by asserting the material-dirty flag around
// gx_render_state_flush.  Caller: FUN_8036deb0.
void gx_force_state_flush(void)
{
    DAT_80612262 = 1;
    gx_render_state_flush();
    DAT_80612262 = 0;
}

// gx_begin_primitive (0x80172818)
// The shared "commit pending GX state, then GXBegin" entry.  The vertex-descriptor
// + format arrive in the spawn frame (FUN_803826f0); count is param_3.  Every 2D
// and immediate-mode drawer funnels its GXBegin through here.
void gx_begin_primitive(int vtxdesc, int fmt, int count)
{
    unsigned char vdesc;
    unsigned char vfmt;   /* extraout_r4 */

    (void)vtxdesc; (void)fmt;
    vdesc = (unsigned char)FUN_803826f0();
    vfmt = 0;                       /* format word arrives in r4 alongside r3 */
    gx_render_state_flush();
    FUN_803c744c(vdesc, vfmt, (unsigned short)count);
    FUN_8038273c();
}

// gx_load_pos_matrix (0x80172894)
// Commit pending render state, then load a GX position matrix (FUN_803cb028).
void gx_load_pos_matrix(unsigned int a, unsigned int b)
{
    gx_render_state_flush();
    FUN_803cb028(a, b);
}

// gx_load_pos_matrix_w (0x8017286c)
// Thin wrapper over gx_load_pos_matrix.
void gx_load_pos_matrix_w(unsigned int a, unsigned int b)
{
    gx_load_pos_matrix(a, b);
}

// gx_batch_depth_range (0x801728e0)
// Program the tex-gen source mode (remapping the small mode id 0/1/2/5 into the
// GX tex-gen constants 9/0xd/0xb/0xa via gx_indirect_matrix_update) and coalesce
// the requested [lo,hi] depth span into the pending span list DAT_8058e598/59c
// (stride 8, count DAT_80612274), merging with any overlapping existing span.
void gx_batch_depth_range(int mode, unsigned int lo, unsigned int stepByte, int step)
{
    unsigned int hi = lo + (stepByte & 0xff) * step;
    int i, off;
    unsigned int count;

    if (mode != 3) {
        if (mode < 3) {
            if (mode == 1)          mode = 0xd;
            else if (mode < 1)   { if (-1 < mode) mode = 9; }
            else                    mode = 0xb;
        } else if ((mode != 5) && (mode < 5)) {
            mode = 10;
        }
    }
    gx_indirect_matrix_update((unsigned int)mode, lo, stepByte);

    count = (unsigned int)DAT_80612274;
    off = 0;
    for (i = count; ; i--) {
        if (i == 0) {
            (&DAT_8058e598)[count * 2] = (int)lo;
            (&DAT_8058e59c)[count * 2] = (int)hi;
            DAT_80612274 = DAT_80612274 + 1;
            return;
        }
        {
            unsigned int spanLo = *(unsigned int *)((int)&DAT_8058e598 + off);
            if (lo < spanLo) {
                if (spanLo <= hi) {
                    *(unsigned int *)((int)&DAT_8058e598 + off) = lo;
                    return;
                }
            } else if ((lo <= *(unsigned int *)((int)&DAT_8058e59c + off)) &&
                       (*(unsigned int *)((int)&DAT_8058e59c + off) < hi)) {
                *(unsigned int *)((int)&DAT_8058e59c + off) = hi;
                return;
            }
        }
        off += 8;
    }
}

// gx_set_num_chans (0x80172a04)
// Read the current GX channel-count state (FUN_803c7694 into a 5-word scratch)
// and, if it differs from the requested value, program the new one (FUN_803c766c).
// Returns the previous value.  Caller: FUN_8018cc90 (render).
unsigned int gx_set_num_chans(unsigned int value)
{
    unsigned int state[5];
    FUN_803c7694(state);
    if (state[0] != value)
        FUN_803c766c(value);
    return state[0];
}

// gx_begin_textured_batch (0x80172a4c)
// Open a textured immediate-mode draw batch.  The primitive-type index (0..5)
// arrives in the spawn frame (__save_gpr_25) and maps to a GX primitive opcode
// (0xb8/0xb0/0x98/0x90/0xa8/0x80); the frame flags select blend tag / indirect
// stage.  Binds the material (param_5 index, -6 = keep current, else bounds
// checked), computes the tex-coord scale for indirect modes, then begins the
// primitive.  Callers: character_nametag_render, projectile_strip_render.
void gx_begin_textured_batch(unsigned int a, unsigned int b, int mode, unsigned short count, int matIndex)
{
    unsigned long long frame;
    int primType = 0;
    (void)a; (void)b;

    frame = __save_gpr_25();
    switch ((int)(frame >> 0x20)) {
        case 0: primType = 0xb8; break;
        case 1: primType = 0xb0; break;
        case 2: primType = 0x98; break;
        case 3: primType = 0x90; break;
        case 4: primType = 0xa8; break;
        case 5: primType = 0x80; break;
    }

    if ((frame & 0x380) == 0) {
        gx_set_blend_tag(1, 0);
    } else if (((frame & 0x80) == 0) && ((frame & 0x100) != 0)) {
        DAT_80612276 = 1;
        gx_indirect_stage_set_lo();
    }

    if (matIndex != -6) {
        if ((matIndex < 0) || (((int*)DAT_80610a20)[3] <= matIndex) || (matIndex <= ((int*)DAT_80610a20)[2]))
            gx_bind_material_fallback();
        else
            DAT_80612254 = *(int*)DAT_80610a20 + matIndex * 0x58;
    }

    if (mode != 0) {
        if ((1 << mode & 0x3c) == 0) {
            if (mode == 1) {
                gx_set_fog_params(0x3016d);ed char)(1 - (((frame & 0x380) == 0) ? 1 : 0));
                FUN_801742a8(0x3016d);
            }
        } else {
            FUN_8017c658();
            FLOAT_806122c8 = FLOAT_806164fc /
                gx_i2f((unsigned int)(*(unsigned short *)(DAT_80612254 + 0x4c) << 4));
            FLOAT_806122c4 = FLOAT_806164fc /
                gx_i2f((unsigned int)(*(unsigned short *)(DAT_80612254 + 0x50) << 4));
        }
    }
    DAT_806109fa = (unsigned char)mode;
    DAT_8061226c = 0;
    FUN_8017fab4();
    gx_begin_primitive(primType, 0, count);
    __restore_gpr_25();
}

// gx_begin_batch_mode5 (0x80172d88)
// Thin wrapper: open a textured batch with primitive-mode 5 (points).
void gx_begin_batch_mode5(unsigned int a, int mode, unsigned short count, int matIndex)
{
    gx_begin_textured_batch(5, a, mode, count, matIndex);
}

// gx_end_batch (0x80172c54)
// Close the current draw batch: commit all pending depth spans (FUN_8039955c),
// tear down the projected-tex / decal state (gx_end/projected teardown), free the
// decal scratch page, restore the indirect stage, restore alpha-compare, reset
// the blend tag.  Callers: character_nametag_render, projectile_strip_render.
void gx_end_batch(void)
{
    int i, off;

    FUN_803826f0();
    if (DAT_80612274 != 0) {
        off = 0;
        for (i = 0; i < (int)(unsigned int)DAT_80612274; i++) {
            FUN_8039955c(*(unsigned int *)((int)&DAT_8058e598 + off),
                         *(int *)((int)&DAT_8058e59c + off) - *(int *)((int)&DAT_8058e598 + off));
            off += 8;
        }
        DAT_80612274 = 0;
    }
    if (DAT_8061226c != 0) {
        if (DAT_8061226c == 1) {
            gx_projtex_flush();
            FUN_803c6ae8(1, 0);
        } else {
            gx_trail_flush();
        }
        if (DAT_8061229c != 0) {
            mem_page_free(DAT_8061229c);
            DAT_8061229c = 0;
        }
    }
    if (DAT_80612276 != 0) {
        gx_indirect_stage_set_lo();
        DAT_80612276 = 0;
    }
    DAT_8061226c = 0;
    if (DAT_806109fa != 0) {
        if ((1 << (int)DAT_806109fa & 0x3c) == 0) {
                gx_set_fog_params(0x43c);
                FUN_801742a8(0x43c);
        } else {
            FUN_8017c670();
        }
    }
    DAT_806109fa = 0xff;
    gx_set_blend_tag(0, 0);
    FUN_8038273c();
}

// gx_begin_projected_batch (0x80172dc4)
// Open a projected-texture / decal draw batch: store the projected-tex quad
// params (FLOAT_806122a0..b0, two scaled by the active view's projection scale at
// DAT_80611d54+0x204+0x28), allocate a scratch page, set begin-mode, bind material
// (matIndex, -ve/out-of-range -> fallback), set the projected-tex scale, mark
// batch mode 1, and program the indirect tex-coord (FUN_803c6ae8).  Returns 1.
unsigned int gx_begin_projected_batch(double p0, double p1, double p2, double p3,
                                      unsigned int a, int matIndex)
{
    unsigned int wasDecal;
    float projScale;

    (void)a;
    FLOAT_806122b0 = (float)p0;
    FLOAT_806122ac = (float)p1;
    wasDecal = countLeadingZeros((unsigned int)(unsigned char)DAT_80612323);
    projScale = *(float *)(*(int *)(DAT_80611d54 + 0x204) + 0x28);
    FLOAT_806122a8 = (float)(p2 * (double)projScale);
    FLOAT_806122a4 = (float)(p3 * (double)projScale);
    DAT_8061229c = mem_page_alloc();

    if ((wasDecal >> 5 == 1) && (DAT_80612290 == 0)) {
        FUN_8017c658();
        DAT_806109fa = 5;
    } else {
        DAT_806109fa = 0;
    }

    if (DAT_80612322 == 0) {
        FUN_8017fab4();
    } else {
        FUN_8017fab4();
        gx_set_blend_tag(1, 0);
    }

    if (DAT_80612323 == 0) {
        if ((matIndex < 0) || (((int*)DAT_80610a20)[3] <= matIndex) || (matIndex <= ((int*)DAT_80610a20)[2]))
            gx_bind_material_fallback();
        else
            DAT_80612254 = *(int*)DAT_80610a20 + matIndex * 0x58;
        FLOAT_806122a0 = FLOAT_8061657c;
    } else {
        FLOAT_806122a0 = FLOAT_806164fc;
    }
    DAT_8061226c = 1;
    FUN_803c6ae8(1, 8);
    return 1;
}


// ===========================================================================
// batch_453 : 0x80172f18 - 0x80174078
// GX projected-texture / decal-trail batch drawers.  These buffer world-space
// points/segments (gx_projtex_add_point / gx_trail_add_point) into the decal
// scratch page allocated by gx_begin_projected_batch / gx_begin_trail_batch, then
// on flush (gx_projtex_flush / gx_trail_flush, also driven by gx_end_batch) emit
// each as a camera-facing billboard quad (gx_projtex_emit_billboard /
// gx_trail_emit_segment) straight into the GX FIFO at 0xCC008000.  When a
// reflect/projection matrix is active (DAT_806109fa != 0) the points are instead
// projected through the env reflect matrix onto the render target.  Companion to
// the batch_452 batch layer above.
//
//   gx_set_projtex_point_mode   0x80172f18  set point(1)/quad(0) emit mode
//   gx_projtex_emit_billboard   0x80172f28  emit one projected-tex quad to FIFO
//   gx_projtex_flush            0x80173520  flush projected-tex point batch
//   gx_projtex_add_point        0x801735cc  buffer one projected-tex point
//   gx_begin_trail_batch        0x80173778  open a trail-strip batch
//   gx_trail_emit_segment       0x80173868  emit one trail segment quad to FIFO
//   gx_trail_add_point          0x80173da4  buffer one trail-strip point
//   gx_trail_flush              0x80173fa0  flush trail-strip point batch
//   gx_set_tev_konst_color      0x8017402c  set GX TEV konst colour (byte-swapped)
//   gx_set_decal_projection_mode 0x80174078 set decal projection mode + post-cb
// ===========================================================================

// ---- batch_453 additional externs ----
extern unsigned char DAT_80612291;   // projtex point-mode flag (0 quad / 1 point)
extern unsigned short DAT_80612298;   // projtex/trail buffered-point count
extern float FLOAT_806122b8;          // trail width
extern float FLOAT_80616580;          // projtex UV scale base
extern float FLOAT_80616584;          // projtex depth scale
extern float FLOAT_80616588;          // sine-table phase scale
extern float FLOAT_8061658c;          // sine-table phase bias
extern float DAT_80583228;            // sin/cos lookup table (0x800 entries)
extern float FLOAT_80616514;          // sine-table index bias
extern void  FUN_803c878c(int slot, unsigned int *color);  // GXSetTevKColor

// gx_set_projtex_point_mode (0x80172f18)
// Set the projected-tex emit mode: 0 = camera-facing quad, 1 = single point.
// Returns the previous mode.  Caller: weather_render_main.
unsigned char gx_set_projtex_point_mode(unsigned char mode)
{
    unsigned char prev = DAT_80612291;
    DAT_80612291 = mode;
    return prev;
}

// gx_projtex_emit_billboard (0x80172f28)
// Emit one buffered projected-texture point as geometry into the GX FIFO.  When
// no reflect/projection matrix is active (DAT_806109fa == 0) the point becomes a
// camera-facing quad built from the view matrix rows (DAT_80611d54+0x210) rotated
// by param_5; when a projection is active it is splatted onto the render target
// using the clip-rect DAT_80611748 and a sine-table rotation.  Point-mode
// (DAT_80612291) emits a single vertex instead of a quad; DAT_80612322 selects an
// untextured (white) vs coloured (param_6, byte-swapped) vertex format.
void gx_projtex_emit_billboard(double x, double y, double z, double size,
                               double rot, unsigned int color)
{
    float scratch[34];
    float wx, wy, wz;   /* rotated half-extent axis */
    float ex, ey, ez;   /* second axis */

    if (DAT_806109fa == 0) {
        float *viewRows = *(float **)(DAT_80611d54 + 0x210);
        float ax = FLOAT_806164f8, ay = FLOAT_806164f8, az = FLOAT_806164f8;
        float bx, by = FLOAT_806164f8, bz = FLOAT_806164f8;
        float fVar5, fVar6, fVar7, fVar1, fVar2, fVar3;
        float fVar8, fVar10, fVar12, fVar9, fVar11, fVar13;
        bx = (float)(size * (double)(FLOAT_806122a0 / (FLOAT_80616580 * **(float **)(DAT_80611d54 + 0x204))));
        by = bx;
        /* build a rotated basis: axis A (ax,ay,az) + axis B (bx,by,bz) */
        {
            float axisA[3]; float axisB[3];
            axisA[0] = bx; axisA[1] = by; axisA[2] = bz;   /* seed local_dc/e4/e0 */
            axisB[0] = ax; axisB[1] = ay; axisB[2] = az;
            mtx4_set_rotation_z(rot, scratch);
            mtx4_multiply(scratch, viewRows, scratch);
            mtx4_transform_vector(scratch, axisA);
            mtx4_transform_vector(scratch, axisB);
            ex = axisB[0]; ey = axisB[1]; ez = axisB[2];
            wx = axisA[0]; wy = axisA[1]; wz = axisA[2];
            ax = ax; ay = ay; az = az;
            bx = axisA[0]; by = axisA[1]; bz = axisA[2];
        }
        fVar5 = (float)(x - (double)wx);
        fVar1 = (float)(x + (double)wx);
        fVar6 = (float)(y - (double)ay);   /* local_d8 = 0 */
        fVar2 = (float)(y + (double)ay);
        fVar7 = (float)(z - (double)az);   /* local_d4 = 0 */
        fVar3 = (float)(z + (double)az);
        fVar8 = fVar5 - ex;  fVar10 = fVar6 - wy; fVar12 = fVar7 - ez;
        fVar5 = fVar5 + ex;  fVar6 = fVar6 + wy;  fVar7 = fVar7 + ez;
        fVar9 = fVar1 + ex;  fVar11 = fVar2 + wy; fVar13 = fVar3 + ez;
        fVar1 = fVar1 - ex;  fVar2 = fVar2 - wy;  fVar3 = fVar3 - ez;

        if (DAT_80612322 == 1) {
            if (DAT_80612291 == 0) {
                GXF(fVar8); GXF(fVar10); GXF(fVar12); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
                GXF(fVar5); GXF(fVar6);  GXF(fVar7);  GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
                GXF(fVar9); GXF(fVar11); GXF(fVar13); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
                GXF(fVar1); GXF(fVar2);  GXF(fVar3);  GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
            } else {
                GXF((float)x); GXF((float)y); GXF((float)z); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            }
        } else if (DAT_80612291 == 0) {
            unsigned int c = gx_bswap32(color);
            GXF(fVar8); GXF(fVar10); GXF(fVar12); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            GXF(fVar5); GXF(fVar6);  GXF(fVar7);  GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
            GXF(fVar9); GXF(fVar11); GXF(fVar13); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
            GXF(fVar1); GXF(fVar2);  GXF(fVar3);  GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
        } else {
            GXF((float)x); GXF((float)y); GXF((float)z); GXU(gx_bswap32(color));
            GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
        }
    } else {
        /* projected onto render target via clip-rect DAT_80611748 + sine rotation */
        float clip1c = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x1c));
        float clip24 = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x24));
        float clip08 = gx_i2f(*(unsigned int *)(DAT_80611748 + 8));
        float clip10 = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x10));
        float clip0c = gx_i2f(*(unsigned int *)(DAT_80611748 + 0xc));
        float rtW    = gx_i2f((unsigned int)DAT_806109c4);
        float rad = (float)((double)FLOAT_80616584 * size) * DAT_8058e40c * (clip0c / rtW);
        float radMax = FLOAT_806122ac * rtW;
        float px = (float)((double)clip10 * x + (double)clip08);
        float py = -(float)((double)clip24 * y - (double)clip1c);
        unsigned int idx;
        float s, cwid;
        if (radMax < rad) rad = radMax;
        idx = (unsigned int)(FLOAT_80616588 * (float)((double)FLOAT_8061658c + rot) + FLOAT_80616514);
        s    = rad * (&DAT_80583228)[idx & 0x7ff];
        cwid = rad * *(float *)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));

        if (DAT_80612322 == 1) {
            if (DAT_80612291 == 0) {
                GXF(px + cwid); GXF(py + s * FLOAT_806122b0);   GXF((float)z); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
                GXF(px + s);    GXF(py - cwid * FLOAT_806122b0); GXF((float)z); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
                GXF(px - cwid); GXF(py - s * FLOAT_806122b0);   GXF((float)z); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
                GXF(px - s);    GXF(py + cwid * FLOAT_806122b0); GXF((float)z); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
            } else {
                GXF(px); GXF(py); GXF((float)z); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            }
        } else if (DAT_80612291 == 0) {
            unsigned int c = gx_bswap32(color);
            GXF(px + cwid); GXF(py + s * FLOAT_806122b0);   GXF((float)z); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            GXF(px + s);    GXF(py - cwid * FLOAT_806122b0); GXF((float)z); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
            GXF(px - cwid); GXF(py - s * FLOAT_806122b0);   GXF((float)z); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
            GXF(px - s);    GXF(py + cwid * FLOAT_806122b0); GXF((float)z); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
        } else {
            GXF(px); GXF(py); GXF((float)z); GXU(gx_bswap32(color)); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
        }
    }
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
}

// gx_projtex_flush (0x80173520)
// Flush the buffered projected-texture points: GXBegin (points 0x80 x4 in quad
// mode, or point-strip 0xb8 in point mode), then emit each buffered point
// (stride 7 floats) via gx_projtex_emit_billboard.  Called from gx_end_batch and
// gx_projtex_add_point on buffer-full.
void gx_projtex_flush(void)
{
    float *pt = (float *)DAT_8061229c;
    unsigned int count = (unsigned int)DAT_80612298;
    int i;

    FUN_803826f0();
    if (count != 0) {
        if (DAT_80612291 == 0)
            gx_begin_primitive(0x80, 0, (int)DAT_80612298 << 2);
        else
            gx_begin_primitive(0xb8, 0, (int)DAT_80612298);
        for (i = 0; i < (int)count; i++) {
            gx_projtex_emit_billboard((double)pt[0], (double)pt[1], (double)pt[2],
                                      (double)pt[4], (double)pt[5], (unsigned int)pt[3]);
            pt += 7;
        }
    }
    DAT_80612298 = 0;
    FUN_8038273c();
}

// gx_projtex_add_point (0x801735cc)
// Buffer one projected-texture point (0x1c-byte stride record).  When no
// reflect/projection is active the point is stored raw; when active it is
// projected through the env reflect matrix (gx_get_env_reflect_matrix +
// mtx4_transform_point4), clip-culled, and stored with perspective-divided coords.
// Flushes automatically at 0x249 buffered points.  Also the shared entry that
// gx_set_fog_range forwards fixed sentinel args to.
void gx_projtex_add_point(double x, double y, double z, double size,
                          double param5, double param6, float param7)
{
    float *rec = (float *)(DAT_8061229c + (unsigned int)DAT_80612298 * 0x1c);

    if (DAT_806109fa == 0) {
        DAT_80612298 = DAT_80612298 + 1;
        rec[0] = (float)x;
        rec[1] = (float)y;
        rec[2] = (float)z;
        rec[4] = (float)size;
    } else {
        float *proj = (float *)gx_get_env_reflect_matrix();
        float in[3];
        float out[6];   /* clip x,y,z,w + spare */
        in[0] = (float)x; in[1] = (float)y; in[2] = (float)z;
        mtx4_transform_point4(proj, in, out);
        if (!((out[2] < -out[3]) || (FLOAT_806164f8 < out[2]))) {
            double invW = (double)(1.0f / out[3]);
            float clampedZ = FLOAT_806122a8;
            DAT_80612298 = DAT_80612298 + 1;
            if (out[2] < FLOAT_806122a8) clampedZ = out[2];
            rec[0] = (float)((double)out[0] * invW);
            rec[1] = (float)((double)out[1] * invW);
            rec[2] = (float)((double)clampedZ * invW + (double)FLOAT_806164fc);
            rec[4] = FLOAT_806122a0 * (float)(invW * size);
        }
    }
    rec[3] = param7;
    rec[5] = (float)param5;
    rec[6] = (float)param6;
    if (DAT_80612298 == 0x249)
        gx_projtex_flush();
}

// gx_begin_trail_batch (0x80173778)
// Open a decal-trail draw batch (batch mode 2): set 2D vertex format + optional
// blend tag, store trail width FLOAT_806122b8, allocate the scratch page, bind
// the material (matIndex out-of-range -> fallback) and enter 2D state, or leave
// begin-mode 0 when projected.  Returns 1.
unsigned int gx_begin_trail_batch(double width, unsigned int param2, int matIndex)
{
    (void)param2;
    if (DAT_80612322 == 0) {
        FUN_8017fab4();
    } else {
        FUN_8017fab4();
        gx_set_blend_tag(1, 0);
    }
    FLOAT_806122b8 = (float)width;
    DAT_8061226c = 2;
    DAT_8061229c = mem_page_alloc();
    if (DAT_80612323 == 0) {
        DAT_806109fa = 5;
        if ((matIndex < 0) || (((int*)DAT_80610a20)[3] <= matIndex) || (matIndex <= ((int*)DAT_80610a20)[2]))
            gx_bind_material_fallback();
        else
            DAT_80612254 = *(int*)DAT_80610a20 + matIndex * 0x58;
        FUN_8017c658();
    } else {
        DAT_806109fa = 0;
    }
    return 1;
}

// gx_trail_emit_segment (0x80173868)
// Emit one trail segment (a (start,end) pair) as a camera-facing quad.  In the
// no-projection path the quad is extruded along the view-plane perpendicular of
// the segment by FLOAT_806122b8 (trail width); in the projection path the segment
// is projected via clip-rect DAT_80611748 and extruded in screen space.
void gx_trail_emit_segment(double x0, double y0, double z0, double x1, double y1, double z1,
                           unsigned int color)
{
    if (DAT_806109fa == 0) {
        float *viewRows = *(float **)(DAT_80611d54 + 0x210);
        float rowB[3]; float rowA[3];
        float w0, w1;
        float ax, ay, az, bx, by, bz;
        float px0, py0, pz0, px1, py1, pz1, px2, py2, pz2, px3, py3, pz3;
        rowB[0] = FLOAT_806164fc; rowB[1] = FLOAT_806164f8; rowB[2] = FLOAT_806164f8;
        rowA[0] = FLOAT_806164f8; rowA[1] = FLOAT_806164fc; rowA[2] = FLOAT_806164f8;
        mtx4_transform_vector(viewRows, rowB);
        mtx4_transform_vector(viewRows, rowA);
        w1 = FLOAT_806122b8 * ((float)(z1 - z0) * rowA[2] + (float)(x1 - x0) * rowA[0] + (float)(y1 - y0) * rowA[1]);
        w0 = FLOAT_806122b8 * ((float)(z1 - z0) * rowB[2] + (float)(x1 - x0) * rowB[0] + (float)(y1 - y0) * rowB[1]);
        ax = rowB[0] * w0; ay = rowB[1] * w0; az = rowB[2] * w0;
        bx = rowA[0] * w1; by = rowA[1] * w1; bz = rowA[2] * w1;
        px0 = (float)(x0 - (double)ax) + bx; py0 = (float)(y0 - (double)ay) + by; pz0 = (float)(z0 - (double)az) + bz;
        px1 = (float)(x1 - (double)ax) + bx; py1 = (float)(y1 - (double)ay) + by; pz1 = (float)(z1 - (double)az) + bz;
        px2 = (float)(x1 + (double)ax) - bx; py2 = (float)(y1 + (double)ay) - by; pz2 = (float)(z1 + (double)az) - bz;
        px3 = (float)(x0 + (double)ax) - bx; py3 = (float)(y0 + (double)ay) - by; pz3 = (float)(z0 + (double)az) - bz;
        if (DAT_80612322 == 0) {
            unsigned int c = gx_bswap32(color);
            GXF(px0); GXF(py0); GXF(pz0); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
            GXF(px1); GXF(py1); GXF(pz1); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
            GXF(px2); GXF(py2); GXF(pz2); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
            GXF(px3); GXF(py3); GXF(pz3); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
        } else {
            GXF(px0); GXF(py0); GXF(pz0); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
            GXF(px1); GXF(py1); GXF(pz1); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
            GXF(px2); GXF(py2); GXF(pz2); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
            GXF(px3); GXF(py3); GXF(pz3); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
        }
    } else {
        float clip24 = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x24));
        float clip1c = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x1c));
        float clip10 = gx_i2f(*(unsigned int *)(DAT_80611748 + 0x10));
        float clip08 = gx_i2f(*(unsigned int *)(DAT_80611748 + 8));
        float sy0 = -(float)((double)clip24 * y0 - (double)clip1c);
        float sy1 = -(float)((double)clip24 * y1 - (double)clip1c);
        float dy = sy1 - sy0;
        float sx0 = (float)((double)clip10 * x0 + (double)clip08);
        float sx1 = (float)((double)clip10 * x1 + (double)clip08);
        float dx = sx1 - sx0;
        if (FLOAT_806164f8 <= dx * dx + dy * dy) {
            dx = dx * FLOAT_806122b8;
            dy = dy * FLOAT_806122b8;
            if (DAT_80612322 == 0) {
                unsigned int c = gx_bswap32(color);
                GXF(sx0 - dy); GXF(sy0 + dx); GXF((float)z0); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
                GXF(sx1 - dy); GXF(sy1 + dx); GXF((float)z1); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
                GXF(sx1 + dy); GXF(sy1 - dx); GXF((float)z1); GXU(c); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
                GXF(sx0 + dy); GXF(sy0 - dx); GXF((float)z0); GXU(c); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            } else {
                GXF(sx0 - dy); GXF(sy0 + dx); GXF((float)z0); GXF(FLOAT_806164f8); GXF(FLOAT_806164fc);
                GXF(sx1 - dy); GXF(sy1 + dx); GXF((float)z1); GXF(FLOAT_806164fc); GXF(FLOAT_806164fc);
                GXF(sx1 + dy); GXF(sy1 - dx); GXF((float)z1); GXF(FLOAT_806164fc); GXF(FLOAT_806164f8);
                GXF(sx0 + dy); GXF(sy0 - dx); GXF((float)z0); GXF(FLOAT_806164f8); GXF(FLOAT_806164f8);
            }
        }
    }
    DAT_80611570 = DAT_80611570 + 4;
    DAT_80611574 = DAT_80611574 + 2;
    DAT_80611578 = DAT_80611578 + 2;
}

// gx_trail_add_point (0x80173da4)
// Buffer one trail-strip point (a start+end pair, 0x1c-byte record).  x0 arrives
// in the spawn frame (FUN_803826f0).  Raw-stored when no projection is active,
// else both endpoints are projected through the env reflect matrix + clip-culled.
// Flushes automatically at 0x249 points.  gx_set_proj_params forwards to this.
void gx_trail_add_point(unsigned int frameArgs, double y0, double z0, double x1, double y1, double z1)
{
    unsigned long long frame;
    float x0f;
    float *rec = (float *)(DAT_8061229c + (unsigned int)DAT_80612298 * 0x1c);

    (void)frameArgs;
    frame = FUN_803826f0();
    x0f = (float)(double)frame;   /* x0 in f1 */

    if (DAT_806109fa == 0) {
        DAT_80612298 = DAT_80612298 + 1;
        rec[0] = x0f;
        rec[1] = (float)y0;
        rec[2] = (float)z0;
        rec[3] = (float)x1;
        rec[4] = (float)y1;
        rec[5] = (float)z1;
    } else {
        float *proj = (float *)gx_get_env_reflect_matrix();
        float inA[3], inB[3];
        float outA[6], outB[6];
        inA[0] = x0f;       inA[1] = (float)y0; inA[2] = (float)z0;
        inB[0] = (float)x1; inB[1] = (float)y1; inB[2] = (float)z1;
        mtx4_transform_point4(proj, inA, outA);
        mtx4_transform_point4(proj, inB, outB);
        if (!((outA[2] < -outA[3]) || (FLOAT_806164f8 < outA[2])) &&
            !((outB[2] < -outB[3]) || (FLOAT_806164f8 < outB[2]))) {
            float invA = 1.0f / outA[3];
            float invB = 1.0f / outB[3];
            DAT_80612298 = DAT_80612298 + 1;
            rec[0] = outA[0] * invA;
            rec[1] = outA[1] * invA;
            rec[2] = outA[2] * invA + FLOAT_806164fc;
            rec[3] = outB[0] * invB;
            rec[4] = outB[1] * invB;
            rec[5] = outB[2] * invB + FLOAT_806164fc;
        }
    }
    rec[6] = x0f;   /* colour word (pt[6] carries r3 = colour from frame) */
    if (DAT_80612298 == 0x249)
        gx_trail_flush();
    FUN_8038273c();
}

// gx_trail_flush (0x80173fa0)
// Flush the buffered trail-strip points: GXBegin (points 0x80 x4), then emit each
// buffered segment (stride 7 floats) via gx_trail_emit_segment.  Called from
// gx_end_batch and gx_trail_add_point on buffer-full.
void gx_trail_flush(void)
{
    float *pt = (float *)DAT_8061229c;
    unsigned int count = (unsigned int)DAT_80612298;
    int i;

    FUN_803826f0();
    if (count != 0) {
        gx_begin_primitive(0x80, 0, (int)DAT_80612298 << 2);
        for (i = 0; i < (int)count; i++) {
            gx_trail_emit_segment((double)pt[0], (double)pt[1], (double)pt[2],
                                  (double)pt[3], (double)pt[4], (double)pt[5], (unsigned int)pt[6]);
            pt += 7;
        }
    }
    DAT_80612298 = 0;
    FUN_8038273c();
}

// gx_set_tev_konst_color (0x8017402c)
// Program the GX TEV konstant colour register (slot 4) from a packed RGBA word
// (byte-reversed into the GX layout).  Callers: gx_set_shadow_mode, outline render.
void gx_set_tev_konst_color(unsigned int color)
{
    unsigned int packed[2];
    packed[0] = ((color & 0xff) << 24) | (((color >> 8) & 0xff) << 16) |
                (((color >> 16) & 0xff) << 8) | ((color >> 24) & 0xff);
    packed[1] = packed[0];
    FUN_803c878c(4, packed);
}

// gx_set_decal_projection_mode (0x80174078)
// Set the decal projection mode (DAT_80612290) and install the matching material
// post-stage callback (0 -> none, else the projected-decal post callback).
// Returns the previous mode.
unsigned char gx_set_decal_projection_mode(int mode)
{
    unsigned char prev = DAT_80612290;
    DAT_80612290 = (unsigned char)mode;
    if (mode == 0)
        gx_set_material_post_cb(0);
    else
        gx_set_material_post_cb(-0x7fe91cb8);
    return prev;
}


// ===========================================================================
// batch_454 : 0x801740c8 - 0x801742d8
// GX batch scratch-page management + fog-parameter render-state.  These manage
// the double-buffered GX display-list / vertex scratch page used by the weather
// and particle submit paths (gx_alloc_batch_page / gx_swap_batch_page), load the
// per-batch position + environment texture matrices, and hold the pending/applied
// fog-parameter word (gx_set/get/apply/invalidate_fog_params).  gx_reset_render_state
// is the top-level per-frame render-state reset that clears all the cached GX
// selection state (material id, matrices, scissor, blend tag) and the indirect
// stages.  Companion to the batch_443-453 material layer above.
//
//   gx_alloc_batch_page       0x801740c8  allocate a 0xc000 GX scratch page
//   gx_swap_batch_page        0x80174144  swap to a fresh scratch page (spawn-frame)
//   gx_load_batch_pos_matrix  0x80174208  load a batch position matrix (blend tag 3)
//   gx_set_tex_matrix_share   0x80174258  set the tex-matrix-share flag (bool from clz)
//   gx_load_env_tex_matrix    0x80174268  transpose + load env tex matrix (slot 0x1e)
//   gx_set_fog_params         0x801742a8  set pending fog-param word, return prev
//   gx_apply_fog_params       0x801742b8  copy pending -> applied fog-param word
//   gx_invalidate_fog_params  0x801742c4  invalidate applied fog-param word (= -1)
//   gx_get_fog_params         0x801742d0  get pending fog-param word
//   gx_reset_render_state     0x801742d8  full per-frame render-state reset
// ===========================================================================

// ---- batch_454 additional externs ----
extern unsigned char DAT_80616590[];   // 3-byte per-index alloc-type table (rodata)

extern unsigned int  FUN_803caf64(void);            // GXGet display-list scratch size
extern void          FUN_803994d0(unsigned int page, int size);  // invalidate/prime page
extern void          FUN_803cae98(int page, int size);           // register GX display list
extern void          FUN_80182170(void);            // batch state post-commit
extern void          FUN_8018204c(void);            // batch state pre-commit / reset
extern void          FUN_80174330(void);            // reset all indirect-texture stages (next batch)

// mem_alloc_aligned / mem_advance_secondary are declared void-returning in the
// shared decls; the batch page allocator uses their (register) return value as a
// pointer.  Re-view them through function-pointer aliases with the real return
// type (same idiom as subobj_build.cpp) without touching the shared header.
typedef int (*MemAllocAlignedFn)(int alignment, int size, int type);
#define MEM_ALLOC_ALIGNED_R ((MemAllocAlignedFn)&mem_alloc_aligned)
typedef int (*MemAdvanceSecondaryFn)(int size);
#define MEM_ADVANCE_SECONDARY_R ((MemAdvanceSecondaryFn)&mem_advance_secondary)

// gx_alloc_batch_page (0x801740c8)
// Allocate a 0x20-aligned 0xc000-byte GX scratch page into *pageOut, prime it
// (FUN_803994d0), record its capacity token in pageOut[1], register it as a GX
// display-list region (FUN_803cae98) and run the post-commit hook (FUN_80182170).
// Sets the suppress-flush flag (DAT_80612323) so the batch fills without an
// intervening state flush.  Callers: weather_submit_particles_to_gx,
// snow_submit_particles_to_gx, FUN_8036e3dc.
void gx_alloc_batch_page(unsigned int* pageOut, unsigned int capToken)
{
    *pageOut = (unsigned int)MEM_ALLOC_ALIGNED_R(0x20, 0xc000, 0);
    FUN_803994d0(*pageOut, 0xc000);
    pageOut[1] = capToken;
    FUN_803cae98((int)*pageOut, 0xc000);
    FUN_80182170();
    DAT_80612323 = 1;
}

// gx_swap_batch_page (0x80174144)
// Close the current GX scratch page and open a fresh one.  The active page ptr +
// its used size arrive via the spawn frame (FUN_803826f0 -> pageSlot[0]=ptr,
// pageSlot[1]=usedFlag).  When the page is empty (usedFlag==0) the new page is a
// cheap secondary-heap advance (mem_advance_secondary); otherwise a full aligned
// allocation sized by the per-mode alloc-type table (DAT_80616590[usedFlag]).  The
// old page contents are copied into the new one (leaf_31e8), the GX display list
// is closed (FUN_8039955c) and the old page freed (mem_free_top).  Finally the
// suppress-flush flag is cleared and the render state reset.  Same callers as
// gx_alloc_batch_page.
void gx_swap_batch_page(void)
{
    unsigned int* pageSlot;
    unsigned int  usedFlag;
    unsigned int  newSize;
    unsigned int  newPage;

    pageSlot = (unsigned int*)FUN_803826f0();
    usedFlag = pageSlot[1];
    newSize  = FUN_803caf64();

    if (usedFlag == 0) {
        newPage = (unsigned int)MEM_ADVANCE_SECONDARY_R((int)newSize);
    } else {
        newPage = (unsigned int)MEM_ALLOC_ALIGNED_R(0x20, (int)newSize,
                                                     (int)(char)DAT_80616590[usedFlag]);
    }

    leaf_31e8((int)newPage, *pageSlot, (int)newSize);
    FUN_8039955c(newPage, (int)newSize);
    mem_free_top((int)*pageSlot);
    *pageSlot   = newPage;
    pageSlot[1] = newSize;
    DAT_80612323 = 0;

    FUN_8018204c();
    gx_reset_render_state();
    gx_set_blend_tag(0, 0);
    FUN_8038273c();
}

// gx_load_batch_pos_matrix (0x80174208)
// Load a batch position matrix: force blend tag 3 (apply-now) so the matrix takes
// effect immediately, load the position matrix from the 2-word descriptor (ptr +
// id), restore the default blend tag and run the batch post-commit hook.
// Callers: weather_render_portal_precipitation, FUN_8018cefc, FUN_8036a938.
void gx_load_batch_pos_matrix(unsigned int* mtxDesc)
{
    gx_set_blend_tag(3, 0);
    gx_load_pos_matrix_w(mtxDesc[0], mtxDesc[1]);
    gx_set_blend_tag(0, 0);
    FUN_80182170();
}

// gx_set_tex_matrix_share (0x80174258)
// Set the tex-matrix-share flag (DAT_8061225c) to a boolean derived from the
// argument via the CLZ>>5 "is-zero" idiom (1 when arg==0, else 0).  When set the
// render-state flush skips reloading the texture matrix (it shares the model/view
// matrix).  Callers: FUN_8036a938, FUN_8036deb0.
void gx_set_tex_matrix_share(unsigned int arg)
{
    DAT_8061225c = (int)(countLeadingZeros(arg) >> 5);
}

// gx_load_env_tex_matrix (0x80174268)
// Transpose the supplied 3x4 matrix into a local, load it into GX texture-matrix
// slot 0x1e (FUN_803cb2cc), then set the fog-parameter word to the fixed env-map
// projection token 0x1041e (gx_set_fog_params).  Callers: FUN_80182934, FUN_8036a938.
void gx_load_env_tex_matrix(float* mtx)
{
    float transposed[18];
    mtx4_transpose(mtx, transposed);
    FUN_803cb2cc(transposed, 0x1e, 0);
    gx_set_fog_params(0x1041e);
}

// gx_set_fog_params (0x801742a8)
// Store the packed fog-parameter word (DAT_80610a04, applied on the next state
// flush) and return the previous value.  Callers: gx_load_env_tex_matrix,
// gx_begin_textured_batch, gx_end_batch, FUN_80182934, FUN_8036deb0.
unsigned int gx_set_fog_params(unsigned int params)
{
    unsigned int prev = DAT_80610a04;
    DAT_80610a04 = params;
    return prev;
}

// gx_apply_fog_params (0x801742b8)
// Mark the pending fog-parameter word as applied (copy pending -> applied) so the
// next state flush treats it as clean.  Callers: FUN_8018cc90, FUN_8018d4ec.
void gx_apply_fog_params(void)
{
    DAT_80610a08 = DAT_80610a04;
}

// gx_invalidate_fog_params (0x801742c4)
// Invalidate the applied fog-parameter word (set to -1) so the next state flush
// re-applies the pending word.  Callers: gx_reset_render_state, FUN_8018d4a8.
void gx_invalidate_fog_params(void)
{
    DAT_80610a08 = 0xffffffff;
}

// gx_get_fog_params (0x801742d0)
// Return the pending fog-parameter word.  Caller: FUN_80182934.
unsigned int gx_get_fog_params(void)
{
    return DAT_80610a04;
}

// gx_reset_render_state (0x801742d8)
// Top-level per-frame render-state reset: mark the projection + model/view
// matrices dirty, invalidate the cached material id / variant, invalidate the
// scissor rect (all four edges = -1), reset the blend tag (0xff = force reapply),
// reset all indirect-texture stages (FUN_80174330) and invalidate the applied fog
// word.  Callers: gx_swap_batch_page, game_main_loop, gx_init_render_mode,
// game_resource_loader_21bc, FUN_8018064c, FUN_80180850.
void gx_reset_render_state(void)
{
    DAT_80612310 = 1;
    DAT_80612250 = 1;
    DAT_806109d0 = 0xffffffff;
    DAT_806109d8 = 0xfffffffe;
    DAT_806109e8 = 0xffffffff;
    DAT_806109ec = 0xffffffff;
    DAT_806109f0 = 0xffffffff;
    DAT_806109f4 = 0xffffffff;
    DAT_80612278 = 0xff;
    FUN_80174330();
    gx_invalidate_fog_params();
}

// ===========================================================================
// batch_457 (part 1/2) : 0x80175240, 0x80175278, 0x801752f4
// Two more deferred draw/resource command builders and the command-list
// EXECUTOR that replays the SoA command arrays (DAT_8058f900..) recorded by the
// gx_cmd_add_* family.  The executor drives the game's asynchronous DVD/resource
// pipeline: for each queued command it dispatches on the type code and calls the
// low-level file services (FUN_803b*), advancing a per-command program counter
// (DAT_8058f93c) until the terminating type-0xe (end) record.  Companion to the
// batch_443-456 GX layer above.
//
//   gx_cmd_add_type12   0x80175240  record a type-0xc command (arg only)
//   gx_cmd_add_type9    0x80175278  record a type-9 command (frame + 2 extras)
//   gx_cmd_list_execute 0x801752f4  step the recorded command list one command
// ===========================================================================

// Two globals owned by the type-9 record path (the "expected reply" pair the
// executor validates against for the 0x1e command).
extern unsigned int DAT_80610a14;   // pending command reply word 0 (init -1)
extern unsigned int DAT_80610a18;   // pending command reply word 1 (init -1)
extern unsigned int DAT_80610a10;   // executor scratch path / handle

// PTR_DAT_8040f1cc: per-command-type "step program" table.  Each entry points at
// an array of sub-step type codes; DAT_8058f93c[idx] indexes into it.
extern unsigned int* PTR_DAT_8040f1cc[];
extern unsigned int  DAT_80612370;   // 6-byte id blob copied into request headers
extern unsigned int  DAT_80612374;   // accumulated status flags
extern int           DAT_8061237c;   // outstanding matrix/async command count

// Low-level DVD/resource service routines invoked by the executor.
extern int  FUN_8039e020(void);
extern int  FUN_803b5d40(unsigned int handle, unsigned int* out, unsigned int* sizeOut);
extern int  FUN_803b65e8(unsigned int handle, unsigned int page, int flag);
extern int  FUN_803b2f08(unsigned int handle);
extern int  FUN_803b5bf8(unsigned int handle, void* p);
extern int  FUN_803b777c(unsigned int handle, const char* name, unsigned int sz, int* out, void* p);
extern int  FUN_803b7fe8(int* out, unsigned int dst, unsigned int sz, unsigned int flags, void* p);
extern int  FUN_803b74d4(unsigned int handle, const char* name, int* out);
extern int  FUN_803b7c84(int* out, unsigned int dst, unsigned int sz, unsigned int flags, void* p);
extern int  FUN_803b66cc(unsigned int handle);
extern int  FUN_803b85d4(unsigned int handle, int entry, unsigned int rec);
extern int  FUN_803b885c(unsigned int handle, int entry, int rec);
extern int  FUN_803b75f0(int* out);
extern int  FUN_803b73d0(unsigned int handle, int arg, int* out);
extern int  FUN_803b6f14(unsigned int handle, void* p);
extern int  FUN_803b82cc(unsigned int handle, const char* name, void* p);
extern int  FUN_803b88a4(unsigned int handle, unsigned int scratch, const char* name, int flag);
extern int  FUN_803b8aa0(unsigned int handle, unsigned int* ptOut);
extern int  FUN_803b81a0(unsigned int handle, int arg, void* p);
extern int  FUN_803b2f38(unsigned int handle, int* w0Out, int* w1Out);
extern int  FUN_803b5c20(unsigned int handle);
extern void FUN_80399500(unsigned int dst, unsigned int alignedSz);
extern void FUN_803994a4(unsigned int dst, unsigned int alignedSz);
extern void FUN_8038270c(void);                     // spawn-frame pop (of 803826c0)
extern unsigned long long FUN_803826c0(void);        // spawn-frame ptr (hi=index, lo=record ptr)
extern void memset_wrapper(int dst, int val, int len);
extern void leaf_31e8(unsigned int dst, unsigned int src, int len);

// gx_cmd_add_type12 (0x80175240)
// Record a type-0xc command: just the target-page arg and cleared flag words.
void gx_cmd_add_type12(unsigned int arg, int idx)
{
    DAT_8058f914[idx] = 0xc;
    DAT_8058f928[idx] = arg;
    DAT_8058f93c[idx] = 0;
    DAT_8058f950[idx] = 0;
}

// gx_cmd_add_type9 (0x80175278)
// Record a type-9 command: spawn-frame slot (index in hi, value in lo), two extra
// words (E1/E2) and reset the pending-reply pair to -1.
void gx_cmd_add_type9(unsigned int arg2, unsigned int arg3, unsigned int e1, unsigned int e2)
{
    unsigned long long frame = FUN_803826f0();
    int idx = (int)frame;
    DAT_8058f914[idx] = 9;
    DAT_8058f928[idx] = (unsigned int)(frame >> 0x20);
    DAT_8058fa7c[idx] = e1;
    DAT_8058fa90[idx] = e2;
    DAT_8058f93c[idx] = 0;
    DAT_8058f950[idx] = 0;
    DAT_80610a14 = 0xffffffff;
    DAT_80610a18 = 0xffffffff;
    FUN_8038273c();
}

// gx_cmd_list_execute (0x801752f4)
// Advance the recorded command list by one command.  The spawn frame supplies the
// command index (hi) and result-slot pointer (lo).  A per-type program table
// (PTR_DAT_8040f1cc) drives a mini state machine; DAT_8058f93c[idx] is the program
// counter into the selected step list, and each case issues the matching DVD/
// resource service call, advancing the PC on success (0) or on the "already
// present" codes (-6/-0xd), retrying on -1, and flagging an error otherwise.
// *pStatusOut receives the current sub-step type; *pReplyOut receives the last
// point-pair reply.  Type 0xe terminates the list.
void gx_cmd_list_execute(unsigned int param_1, unsigned int param_2,
                         unsigned int* pStatusOut, unsigned int* pReplyOut)
{
    unsigned char auStack_120[108];
    unsigned char auStack_b4[40];
    unsigned char auStack_8c[4];
    unsigned char auStack_88[2];
    unsigned char local_86;
    unsigned int local_84;
    unsigned short local_80;
    unsigned short local_7e;
    unsigned int local_7c;
    int local_128;
    unsigned int local_124;

    unsigned long long frame = FUN_803826c0();
    int iVar4 = (int)(frame >> 0x20);          // command index
    int* piVar7 = (int*)(unsigned int)frame;   // result-code slot
    int idx = iVar4;
    bool bErr = false;
    int rc = 0;

    unsigned int* stepList = PTR_DAT_8040f1cc[DAT_8058f914[idx]];
    DAT_80612374 = DAT_80612374 | FUN_8039e020();

    if ((DAT_806122d0 != 0) && ((DAT_806122d0 & (1 << iVar4)) == 0)) {
        if (piVar7 != 0) *piVar7 = -1;
        if (pStatusOut != 0) *pStatusOut = 0x12;
        FUN_8038270c();
        return;
    }

    if (DAT_8058f93c[idx] == 0) {
        DAT_806122d0 = 1 << iVar4;
    }
    if (pStatusOut != 0) {
        *pStatusOut = stepList[DAT_8058f93c[idx]];
    }

    switch (stepList[DAT_8058f93c[idx]]) {
    case 1:
        rc = FUN_803b5d40(DAT_8058f928[idx], (unsigned int*)(idx * 4 - 0x7fa70530 + iVar4 * 0),
                          &DAT_8058fae4[idx]);
        if (rc == 0) {
            if (DAT_8058fae4[idx] == 0x2000) {
                DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
            } else {
                rc = -0x1a7c;
                bErr = true;
            }
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 2:
        rc = FUN_803b65e8(DAT_8058f928[idx], DAT_8058f900[iVar4], 0);
        if ((rc == -6) || (rc == -0xd)) {
            DAT_8058f950[idx] = DAT_8058f950[idx] | 1;
        }
        if (rc == 0) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        }
        break;
    case 3:
        rc = FUN_803b2f08(DAT_8058f928[idx]);
        if ((rc == -6) || (rc == -0xd) || (rc == 0)) {
            DAT_8058f950[idx] = DAT_8058f950[idx] | 1;
        }
        if ((rc == 0) || (rc == -6) || (rc == -0xd)) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 4:
        rc = FUN_803b5bf8(DAT_8058f928[idx], 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 5: case 7: case 9: case 0xc:
    case 0x17: case 0x19: case 0x1c: case 0x21:
        rc = FUN_803b2f08(DAT_8058f928[idx]);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 6:
        rc = FUN_803b777c(DAT_8058f928[idx], (const char*)(iVar4 * 0x20 - 0x7fa7069c),
                          DAT_8058fa18[idx], (int*)(iVar4 * 0x14 - 0x7fa70508), 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 8:
        FUN_80399500(DAT_8058fa04[idx], (DAT_8058fa18[idx] + 0x1f) & 0xffffffe0);
        rc = FUN_803b7fe8((int*)(iVar4 * 0x14 - 0x7fa70508), DAT_8058fa04[idx],
                          DAT_8058fa18[idx], DAT_8058fa2c[idx], 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 10:
        rc = FUN_803b74d4(DAT_8058f928[idx], (const char*)(iVar4 * 0x20 - 0x7fa7069c),
                          (int*)(iVar4 * 0x14 - 0x7fa70508));
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0xb:
        FUN_803994a4(DAT_8058fa04[idx], (DAT_8058fa18[idx] + 0x1f) & 0xffffffe0);
        rc = FUN_803b7c84((int*)(iVar4 * 0x14 - 0x7fa70508), DAT_8058fa04[idx],
                          DAT_8058fa18[idx], DAT_8058fa2c[idx], 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0xd:
        rc = FUN_803b66cc(DAT_8058f928[idx]);
        if (rc == 0) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
            DAT_8058f950[idx] = DAT_8058f950[idx] & 0xfffffffe;
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 0xe: {
        int t = DAT_8058f914[idx];
        DAT_806122d0 = 0;
        bErr = false;
        rc = 0;
        if (((t == 2) || (t == 1) || (t == 10)) && (0 < DAT_8061237c)) {
            DAT_8061237c = DAT_8061237c - 1;
        }
        break;
    }
    case 0xf:
        memset_wrapper((int)auStack_b4, 0, 0x6c);
        rc = FUN_803b85d4(DAT_8058f928[idx], *(int*)(&DAT_8058fafc + iVar4 * 0x14), (unsigned int)auStack_b4);
        if (rc == 0) {
            local_80 = (unsigned short)((local_80 & 0xfffc) | 2);
            local_7e = (unsigned short)((local_7e & 0xfff0) | 3);
            local_7c = 0;
            local_84 = 0x40;
            local_86 = 0xfb;
            leaf_31e8((unsigned int)auStack_8c, DAT_80612370, 4);
            leaf_31e8((unsigned int)auStack_88, DAT_80612370 + 4, 2);
            rc = FUN_803b885c(DAT_8058f928[idx], *(int*)(&DAT_8058fafc + iVar4 * 0x14), (int)auStack_b4);
            if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
            else bErr = true;
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 0x10:
        rc = FUN_803b75f0((int*)(iVar4 * 0x14 - 0x7fa70508));
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x11:
        rc = FUN_803b73d0(DAT_8058f928[idx], DAT_8058fa68[idx], (int*)(iVar4 * 0x14 - 0x7fa70508));
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x13: {
        for (int e = 0; e < 0x7f; e = e + 1) {
            int r = FUN_803b85d4(DAT_8058f928[idx], e, (unsigned int)auStack_120);
            if (r == 0) {
                if (DAT_8058fa7c[idx] != 0) {
                    int (*cb)(void*, int, unsigned int, unsigned int, unsigned int) =
                        (int (*)(void*, int, unsigned int, unsigned int, unsigned int))DAT_8058fa7c[idx];
                    r = cb(auStack_120, e, DAT_8058faa8[iVar4 * 2], DAT_8058faac[iVar4 * 2], DAT_8058fa90[idx]);
                    if (r == 0) break;
                }
            } else if (r == -1) {
                e = e - 1;
            }
        }
        DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        break;
    }
    case 0x14:
        rc = FUN_803b6f14(DAT_8058f928[idx], 0);
        if (rc == 0) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            do {
                int pc = DAT_8058f93c[idx];
                DAT_8058f93c[idx] = pc + 1;
                if (stepList[pc] == 0xe) break;
            } while (1);
        }
        break;
    case 0x15:
        rc = FUN_803b2f08(DAT_8058f928[idx]);
        if ((rc == -6) || (rc == -0xd) || (rc == 0)) {
            DAT_8058f950[idx] = DAT_8058f950[idx] | 1;
        }
        if (rc == 0) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            do {
                int pc = DAT_8058f93c[idx];
                DAT_8058f93c[idx] = pc + 1;
                if (stepList[pc] == 0xe) break;
            } while (1);
        }
        break;
    case 0x16:
        rc = FUN_803b82cc(DAT_8058f928[idx], (const char*)DAT_80610a10, 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc == -4) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 2;
        else if (rc != -1) bErr = true;
        break;
    case 0x18:
        rc = FUN_803b88a4(DAT_8058f928[idx], DAT_80610a10, (const char*)(iVar4 * 0x20 - 0x7fa7069c), 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x1a:
        rc = FUN_803b74d4(DAT_8058f928[idx], (const char*)DAT_80610a10, (int*)(iVar4 * 0x14 - 0x7fa70508));
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x1b:
        rc = FUN_803b82cc(DAT_8058f928[idx], (const char*)(iVar4 * 0x20 - 0x7fa7069c), 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc == -4) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 2;
        else if (rc != -1) bErr = true;
        break;
    case 0x1d:
        rc = FUN_803b777c(DAT_8058f928[idx], (const char*)DAT_80610a10, DAT_8058fa18[idx],
                          (int*)(iVar4 * 0x14 - 0x7fa70508), 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x1e: {
        int p = iVar4 * 2;
        if (DAT_8058faac[p] == DAT_8058fa44[p] && DAT_8058faa8[p] == DAT_8058fa40[p]) {
            rc = 0;
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else {
            rc = -0x1a7a;
            bErr = true;
        }
        break;
    }
    case 0x1f:
        rc = FUN_803b8aa0(DAT_8058f928[idx], &DAT_8058faa8[iVar4 * 2]);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x20:
        rc = FUN_803b81a0(DAT_8058f928[idx], DAT_8058fa68[idx], 0);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc == -4) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 2;
        else if (rc != -1) bErr = true;
        break;
    case 0x22:
        rc = FUN_803b2f38(DAT_8058f928[idx], (int*)&local_124, &local_128);
        if (rc == 0) {
            unsigned int need = DAT_8058fa18[idx] + 0x1fff;
            int haveBlocks = ((int)local_124 >> 0xd) + (((int)local_124 < 0 && (local_124 & 0x1fff) != 0) ? 1 : 0);
            int needBlocks = ((int)need >> 0xd) + (((int)need < 0 && (need & 0x1fff) != 0) ? 1 : 0);
            if ((local_128 < 1) || (haveBlocks < needBlocks)) {
                do {
                    int pc = DAT_8058f93c[idx];
                    DAT_8058f93c[idx] = pc + 1;
                    if (stepList[pc] == 0xe) break;
                } while (1);
            } else {
                DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
            }
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 0x23:
        rc = FUN_803b5c20(DAT_8058f928[idx]);
        if ((rc == 0) || (rc == -6) || (rc == -0xd)) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    case 0x24:
        rc = FUN_803b2f08(DAT_8058f928[idx]);
        if ((rc == -6) || (rc == -0xd)) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc != -1) {
            if (rc == 0) rc = -0x1a7a;
            bErr = true;
        }
        break;
    case 0x25:
        rc = FUN_803b2f38(DAT_8058f928[idx], (int*)&DAT_80610a14, (int*)&DAT_80610a18);
        if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        else if (rc != -1) bErr = true;
        break;
    case 0x26: {
        const char* name = (const char*)(iVar4 * 0x20 - 0x7fa7069c);
        int* out = (int*)(iVar4 * 0x14 - 0x7fa70508);
        rc = FUN_803b74d4(DAT_8058f928[idx], name, out);
        if (rc == 0) {
            DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
        } else if (rc == -4) {
            rc = FUN_803b777c(DAT_8058f928[idx], name, DAT_8058fa18[idx], out, 0);
            if (rc == 0) DAT_8058f93c[idx] = DAT_8058f93c[idx] + 1;
            else if (rc != -1) bErr = true;
        } else if (rc != -1) {
            bErr = true;
        }
        break;
    }
    default:
        break;
    }

    if (piVar7 != 0) *piVar7 = rc;
    if (pReplyOut != 0) {
        pReplyOut[1] = DAT_8058faac[iVar4 * 2];
        pReplyOut[0] = DAT_8058faa8[iVar4 * 2];
    }
    {
        int t = DAT_8058f914[idx];
        if (bErr && ((t == 2) || (t == 1) || (t == 10)) && (0 < DAT_8061237c)) {
            DAT_8061237c = DAT_8061237c - 1;
        }
    }
    if (bErr || (stepList[DAT_8058f93c[idx]] == 0xe)) {
        DAT_806122d0 = 0;
    }
    FUN_8038270c();
}

// ============================================================================
// batch_463  0x8017c470 - 0x8017c6e8  | Date: 2026-09-06
// GX 2D / ortho render-mode state layer.  These are the low-level begin/end and
// projection-setup helpers that every 2D drawer (text, sprites, particles, HUD,
// gx_draw2d) and gx_render_state_flush funnel through.  Appended to
// gx_material_state.cpp (subsystem:graphics, tag gx-material) since the 2D-mode
// state globals (DAT_80612254/310/314/318/319/31c, DAT_8058fc*) live here.
//
//   gx_2d_state_nop           0x8017c470  empty
//   gx_material_push_pop      0x8017c474  push/pop active material for 2D (depth ctr)
//   gx_build_ortho_2d_matrix  0x8017c4cc  build the screen-space ortho projection
//   gx_enter_2d_mode          0x8017c540  set 2D modelview + ortho projection + near/far
//   gx_exit_2d_mode           0x8017c5c0  restore 3D projection / model / tex matrices
//   gx_begin_2d_draw          0x8017c658  begin a 2D draw region (nesting counter)
//   gx_end_2d_draw            0x8017c670  end a 2D draw region
//   gx_reset_env_tex_matrices 0x8017c68c  zero the 3 env-tex reset matrices
//   lens_flare_slot_reset_init 0x8017c6e8 reset lens-flare slot table + preload
// ----------------------------------------------------------------------------

extern void  FUN_803a142c(double a, double b, double c, double d, double e, double f, float* out);
extern int   DAT_806122e0;    // saved active material (2D push/pop)
extern int   DAT_806122e4;    // 2D material push depth counter
extern int   DAT_8061231c;    // 2D draw-region nesting counter
extern float DAT_8058fca0;    // lens-flare slot table base (8 x 0x10)
extern float FLOAT_8061663c;  // ortho constant (1.0-ish)
extern float FLOAT_80616638;  // half-texel bias
extern float FLOAT_80616648;  // ortho depth range constant
extern double DOUBLE_80616650; // int->double magic bias

// gx_material_push_pop (0x8017c474)
// Push (leave==0) or pop (leave!=0) the active material selection for a nested 2D
// draw: on the first push the current material DAT_80612254 is saved to DAT_806122e0
// and cleared; the matching pop restores it.  DAT_806122e4 is the nesting depth.
void gx_material_push_pop(int leave)
{
    if (leave == 0) {
        if (DAT_806122e4 == 0) {
            DAT_806122e0 = DAT_80612254;
        }
        DAT_80612254 = 0;
        DAT_806122e4 = DAT_806122e4 + 1;
        return;
    }
    if (DAT_806122e4 == 1) {
        DAT_80612254 = DAT_806122e0;
    }
    DAT_806122e4 = DAT_806122e4 - 1;
}

// gx_build_ortho_2d_matrix (0x8017c4cc)
// Build the screen-space orthographic projection matrix into `out`: left/top = 0,
// right = screen width DAT_806109c4, bottom = screen height DAT_806109c8, near/far
// from FLOAT_8061663c / FLOAT_80616648.  Uses the int->float magic-bias idiom.
void gx_build_ortho_2d_matrix(float* out)
{
    double one = (double)FLOAT_8061663c;
    double bottom = (double)((float)((double)((unsigned long long)0x4330000000000000ULL |
                    (unsigned long long)(unsigned int)(DAT_806109c8 ^ 0x80000000)) - DOUBLE_80616650)
                    - FLOAT_80616638);
    double right = (double)((float)((double)((unsigned long long)0x4330000000000000ULL |
                    (unsigned long long)(unsigned int)(DAT_806109c4 ^ 0x80000000)) - DOUBLE_80616650)
                    - FLOAT_80616638);
    FUN_803a142c(one, bottom, one, right, one, (double)FLOAT_80616648, out);
}

// gx_enter_2d_mode (0x8017c540)
// Enter 2D / ortho render mode: build an identity modelview and the screen-space
// ortho projection, copy the current far-plane params, program the clear/near-far
// register (FUN_803cb468), load model (slot 0) and projection (slot 1) matrices, and
// set the ortho-active flag DAT_80612318.  Called by gx_render_state_flush and
// FUN_8018008c.
void gx_enter_2d_mode(void)
{
    float ortho[16];
    float model[14];
    FUN_803a10e8(model);
    gx_build_ortho_2d_matrix(ortho);
    DAT_8058fc20 = DAT_8058fc38;
    DAT_8058fc24 = DAT_8058fc3c;
    FUN_803cb468((double)DAT_8058fc10, (double)DAT_8058fc14, (double)DAT_8058fc18,
                 (double)DAT_8058fc1c, (double)DAT_8058fc38, (double)DAT_8058fc3c);
    FUN_803cb1c8(model, 0);
    FUN_803cb098(ortho, 1);
    DAT_80612318 = 1;
}

// gx_exit_2d_mode (0x8017c5c0)
// Leave 2D / ortho render mode and restore the 3D pipeline: reprogram the perspective
// clear/near-far register from the stored 3D frustum (DAT_8058fc28..34), reload the
// stored model (DAT_8058e3cc) and env-tex (slot 0) matrices, and the 3D projection
// (DAT_8058e40c) honouring the reflect flag DAT_80612321.  Clears the ortho/matrix
// dirty flags.  Called by gx_render_state_flush.
void gx_exit_2d_mode(void)
{
    FUN_803cb468((double)DAT_8058fc28, (double)DAT_8058fc2c,
                 (double)(DAT_8058fc30 - FLOAT_80616638), (double)(DAT_8058fc34 - FLOAT_80616638),
                 (double)DAT_8058fc38, (double)DAT_8058fc3c);
    DAT_80612314 = 0;
    FUN_803cb1c8((float*)&DAT_8058e3cc, 0);
    FUN_803cb248((float*)&DAT_8058e3cc, 0);
    DAT_80612250 = 0;
    FUN_803cb098(&DAT_8058e40c, (unsigned int)DAT_80612321);
    DAT_80612310 = 0;
    DAT_80612318 = 0;
}

// gx_begin_2d_draw (0x8017c658)
// Begin a 2D immediate-draw region: set the 2D-active flag DAT_80612319 and bump the
// nesting counter DAT_8061231c.  THE shared 2D-begin funnel used by text rendering,
// sprites, particle overlays, and every gx_draw2d primitive.
void gx_begin_2d_draw(void)
{
    DAT_80612319 = 1;
    DAT_8061231c = DAT_8061231c + 1;
}

// gx_end_2d_draw (0x8017c670)
// End a 2D immediate-draw region: decrement the nesting counter; when it reaches 0,
// clear the 2D-active flag.
void gx_end_2d_draw(void)
{
    DAT_8061231c = DAT_8061231c - 1;
    if (DAT_8061231c != 0) {
        return;
    }
    DAT_80612319 = 0;
}

// gx_reset_env_tex_matrices (0x8017c68c)
// Zero the three environment-texture reset matrices (0x20 bytes each at
// 0x8058fc40 / 0x8058fc60 / 0x8058fc80).  Called from effect init.
void gx_reset_env_tex_matrices(void)
{
    memset_wrapper((int)0x8058fc40, 0, 0x20);
    memset_wrapper((int)0x8058fc60, 0, 0x20);
    memset_wrapper((int)0x8058fc80, 0, 0x20);
}

// lens_flare_slot_reset_init (0x8017c6e8)
// Reset the 8-slot lens-flare table (DAT_8058fca0, stride 0x10; first word = -1
// sentinel) and preload the lens-flare textures.  Called from effect init FUN_801c1888.
void lens_flare_slot_reset_init(void)
{
    int off = 0;
    for (int i = 8; i != 0; i--) {
        *(unsigned int*)((int)&DAT_8058fca0 + off) = 0xffffffff;
        off += 0x10;
    }
    lens_flare_preload_textures();
}

// gx_2d_state_nop (0x8017c470)
// Empty GX 2D-state callback stub.
void gx_2d_state_nop(void) {}
    lens_flare_preload_textures(0, 0, 0, 0, 0, 0, 0, 0);
}extern double DOUBLE_80616650; // int->double magic bias
extern float DAT_8058fc20, DAT_8058fc24;  // ortho stashed near/far