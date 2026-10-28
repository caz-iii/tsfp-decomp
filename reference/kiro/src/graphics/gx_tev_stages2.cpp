// gx_tev_stages2.cpp
// Second group of per-material TEV-stage builder callbacks (continuation of
// gx_tev_stages.cpp). These program the indirect-TEV heat/refraction and
// screen-distortion material shaders, plus a bone-gated konst stage. Each takes
// the next free colour/coord TEV stage counters (usually via the spill frame)
// and programs stage(s) for a specific distortion effect.
//
// Batch: batch_496
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
extern void gx_set_konst_alpha_sel(int sel);
extern void gx_cached_set_tev_order(unsigned int stage, int texMap, int texCoord, int channel);
extern void gx_cached_set_tev_order_ex(unsigned int stage, int a2, int a3, int a4, int a5, int a6);
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_bind_material_fallback(void);
extern int  sprite_get_texture_id(unsigned int spriteId);
extern unsigned int rng_counter_tick(void);
extern void mtx4_set_identity(int outMtx);
extern void memset_wrapper(int dst, int val, int len);

extern void FUN_803ca174(int slot, unsigned int *rgba);     // set TEV konst colour
extern void FUN_803cb2cc(float *mtx, int slot, int a3);     // GXLoadTexMtxImm
extern void FUN_803c9288(unsigned int *tex, int slot);      // bind tex to slot
extern void FUN_803c9ecc(int n);                            // indirect: set num stages
extern void FUN_803c9de0(int a1, int a2, int a3);           // indirect stage tex config
extern void FUN_803c9c9c(int a1, int a2, int a3);           // indirect stage coord config
extern void FUN_803c9b24(int coord, float *mtx, char a3);   // program tex-coord gen
extern void FUN_803c9ab8(unsigned int stage, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                         int a9, int a10);                  // indirect stage full config
extern void FUN_803c9ef0(int stage);                        // indirect stage disable
extern void FUN_803a1214(double a, double b, double c, float *outMtx);  // 2D tex-gen matrix build

extern int  FUN_803826e4(void);            // spawn-frame accessor (packed pair)
extern void FUN_80382730(void);
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);

// ---- Data ----
extern int   DAT_80611b28;    // current bone index
extern int   DAT_805acbd0;    // bone-konst match id A
extern int   DAT_805acbd4;    // bone-konst match id B
extern int   DAT_805acbd8;    // bone-konst match id C
extern unsigned int DAT_805acbdc;  // packed bone-konst colour source
extern int   DAT_806124a8;    // heat-distortion enable gate
extern float FLOAT_806124ac;  // distortion scale
extern int  *DAT_80610a20;    // render-target group table ([0]=slot base)
extern unsigned int *DAT_80612254;  // active material pointer
extern int   DAT_80611b24;    // camera/entity chain root
extern unsigned int DAT_806109c4;  // screen width
extern unsigned int DAT_806109c8;  // screen height

extern float  FLOAT_80616878;
extern float  FLOAT_8061686c;
extern float  FLOAT_80616870;
extern float  FLOAT_80616890;   // rng divisor
extern float  FLOAT_806168a0;
extern float  FLOAT_806168a4;
extern float  FLOAT_806168a8;
extern float  FLOAT_806168ac;
extern float  FLOAT_806168b0;
extern float  FLOAT_806168b4;
extern float  FLOAT_806168b8;
extern float  FLOAT_806168bc;
extern float  FLOAT_806168c0;
extern double DOUBLE_80616888;  // int->double magic bias
extern double DOUBLE_80616898;  // int->double magic bias (rng)
extern float  DAT_803cd090;     // heat-distort rodata consts
extern float  DAT_803cd094;
extern float  DAT_803cd098;
}

// ---------------------------------------------------------------------------
// Address: 0x8018e4f4 | Name: gx_tev_stage_bone_konst | Date: 2026-09-06
// If the current bone (DAT_80611b28) matches one of the configured bone ids,
// program a konst colour (slot 3) from the packed source DAT_805acbdc and set
// the konst-alpha selector to 6. Returns its stage argument unchanged.
// ---------------------------------------------------------------------------
int gx_tev_stage_bone_konst(int stage)
{
    if (DAT_805acbd0 == DAT_80611b28 || DAT_805acbd4 == DAT_80611b28 ||
        DAT_805acbd8 == DAT_80611b28) {
        unsigned int packed = ((((DAT_805acbdc >> 0x13) & 0xff) << 16) |
                               (((DAT_805acbdc >> 0xb) & 0xff) << 8) |
                               ((DAT_805acbdc >> 2) & 0xff)) & 0x1f0fff;
        unsigned int konst = (packed & 0xffff0f) << 8;
        gx_set_konst_alpha_sel(6);
        unsigned int sel = konst;
        FUN_803ca174(3, &sel);
    }
    return stage;
}

// ---------------------------------------------------------------------------
// Address: 0x8018e598 | Name: gx_konst_alpha_sel_default_b | Date: 2026-09-06
// Restore the default (0xf) konst-alpha selector.
// ---------------------------------------------------------------------------
void gx_konst_alpha_sel_default_b(void)
{
    gx_set_konst_alpha_sel(0xf);
}

// ---------------------------------------------------------------------------
// Address: 0x8018e5bc | Name: gx_tev_stage_heat_distort | Date: 2026-09-06
// Heat/refraction distortion TEV stage. Gated on DAT_806124a8. Builds an
// rng-scrolled tex-gen matrix from the projector offset (relative to the camera
// body), a konst colour, and programs one indirect TEV stage against sprite
// 0x28b. Stage counters arrive via the spill frame (colour) and *pStage (coord).
// ---------------------------------------------------------------------------
void gx_tev_stage_heat_distort(int p1, int p2, unsigned int *pStage, double fpArg)
{
    (void)p1;
    (void)p2;
    (void)fpArg;
    unsigned long long frame = __save_gpr_25();
    unsigned int colorStage = (unsigned int)((unsigned long long)frame >> 0x20);
    int *pColor = (int *)frame;

    if (DAT_806124a8 != 0) {
        int color0 = *pColor;
        unsigned int colorStage1 = (unsigned int)(color0 + 1);
        unsigned int coordStage = *pStage + 1;
        int tex = sprite_get_texture_id(0x28b);
        int slotBase = *DAT_80610a20;

        float ox = DAT_803cd090, oy = DAT_803cd094, oz = DAT_803cd098;
        unsigned int rngv = rng_counter_tick();
        int body = *(int *)(DAT_80611b24 + 4);
        ox -= *(float *)(body + 0x2c);
        oy -= *(float *)(body + 0x30);
        oz -= *(float *)(body + 0x34);
        double scroll = (double)(FLOAT_806168a4 *
                                 ((float)((double)CONCAT44(0x43300000, rngv) - DOUBLE_80616898) /
                                  FLOAT_80616890) +
                                 FLOAT_806168a0);

        float texMtx[16];
        mtx4_set_identity((int)texMtx);
        texMtx[0]  = FLOAT_806168a8;
        texMtx[3]  = FLOAT_8061686c - ox / FLOAT_806168b0;
        texMtx[5]  = FLOAT_806168a8;
        texMtx[7]  = FLOAT_80616870 - oy / FLOAT_806168b4;
        texMtx[8]  = FLOAT_806168ac;
        texMtx[9]  = FLOAT_80616878;
        texMtx[11] = FLOAT_8061686c - oz / FLOAT_806168b0;

        unsigned char kr = (unsigned char)(int)((double)FLOAT_806168b8 * scroll);
        unsigned char kg = (unsigned char)(int)((double)FLOAT_806168bc * scroll);
        unsigned char kb = (unsigned char)(int)((double)FLOAT_806168c0 * scroll);
        unsigned int konst = ((unsigned int)kr << 24) | ((unsigned int)kg << 16) |
                             ((unsigned int)kb << 8);
        unsigned int sel = konst;
        FUN_803ca174(1, &sel);
        FUN_803cb2cc(texMtx, 0x30, 1);
        gx_cached_set_tev_order_ex(colorStage1, 1, 0, 0x30, 0, 0x7d);
        FUN_803c9288((unsigned int *)(slotBase + tex * 0x58), coordStage);
        gx_cached_set_tev_color_op(colorStage, colorStage1, coordStage, 0xff);
        gx_cached_set_tev_color_in(colorStage, 0xf, 2, 8, 0);
        gx_cached_set_tev_alpha_in(colorStage, 7, 7, 7, 0);
        gx_cached_set_tev_color_env(colorStage, 0, 0, 0, 1, 0);
        gx_cached_set_tev_alpha_env(colorStage, 0, 0, 0, 1, 0);
        *pColor = color0 + 2;
        *pStage = coordStage;
    }
    __restore_gpr_25();
}

// Shared body for the two texture-indexed distortion stages (0x3b8 / 0x2a6).
static void gx_tev_stage_distort_tex(unsigned int *ROM, unsigned int spriteId)
{
    unsigned long long frame = FUN_803826e4();
    unsigned int combineStage = (unsigned int)((unsigned long long)frame >> 0x20);
    unsigned int *pColor = (unsigned int *)frame;

    // Copy 3 packed pairs from the ROM constant table onto a scratch matrix.
    float mtx[6];
    {
        unsigned int *dst = (unsigned int *)mtx - 1;
        unsigned int *src = ROM;
        for (int i = 3; i != 0; i--) {
            unsigned int a = src[1];
            unsigned int b = src[2];
            src += 2;
            dst[1] = a;
            dst += 2;
            *dst = b;
        }
    }
    unsigned int colorStage0 = *pColor; *pColor = colorStage0 + 1;
    unsigned int colorStage1 = *pColor; *pColor = colorStage1 + 1;
    // pCoord counters advanced via the caller's own frame pointer.
    unsigned int coord0 = colorStage0;   // placeholder; real coord counters below
    (void)coord0;

    gx_cached_set_tev_order(colorStage0, 1, 4, 0x3c);
    float scratchMtx[18];
    memset_wrapper((int)scratchMtx, 0, 0x30);
    FUN_803cb2cc(scratchMtx, 0x2a, 1);
    gx_cached_set_tev_order(colorStage1, 1, 4, 0x2a);
    gx_bind_material_fallback();
    FUN_803c9288(DAT_80612254, colorStage0);
    int tex = sprite_get_texture_id(spriteId);
    FUN_803c9288((unsigned int *)(*DAT_80610a20 + tex * 0x58), colorStage1);
    FUN_803c9ecc(1);
    FUN_803c9de0(0, colorStage0, colorStage0);
    FUN_803c9c9c(0, 0, 0);
    FUN_803c9b24(1, mtx, '\x01');
    FUN_803c9ab8(combineStage, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    gx_cached_set_tev_color_op(combineStage, colorStage1, colorStage1, 4);
    gx_cached_set_tev_color_in(combineStage, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_alpha_in(combineStage, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(combineStage, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_env(combineStage, 0, 0, 1, 1, 0);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// Address: 0x8018e850 | Name: gx_tev_stage_distort_tex3b8 | Date: 2026-09-06
// Indirect-distortion TEV stage using distortion texture 0x3b8. Sets up two
// TEV orders (texcoords 0x3c/0x2a), binds the active material + sprite 0x3b8,
// and programs the indirect combine stage.
// ---------------------------------------------------------------------------
void gx_tev_stage_distort_tex3b8(int p1, int p2, unsigned int *pStage)
{
    (void)p1;
    (void)p2;
    (void)pStage;
    gx_tev_stage_distort_tex((unsigned int *)0x803cd098, 0x3b8);
}

// ---------------------------------------------------------------------------
// Address: 0x8018ea64 | Name: gx_tev_stage_distort_tex2a6 | Date: 2026-09-06
// Indirect-distortion TEV stage using distortion texture 0x2a6 (same layout as
// gx_tev_stage_distort_tex3b8, different distortion pattern).
// ---------------------------------------------------------------------------
void gx_tev_stage_distort_tex2a6(int p1, int p2, unsigned int *pStage)
{
    (void)p1;
    (void)p2;
    (void)pStage;
    gx_tev_stage_distort_tex((unsigned int *)0x803cd0b0, 0x2a6);
}

// ---------------------------------------------------------------------------
// Address: 0x8018ec78 | Name: gx_tev_distort_teardown | Date: 2026-09-06
// Tear down the distortion indirect stage: set num indirect stages to 0 and
// disable stage 0.
// ---------------------------------------------------------------------------
void gx_tev_distort_teardown(void)
{
    FUN_803c9ecc(0);
    FUN_803c9ef0(0);
}

// ---------------------------------------------------------------------------
// Address: 0x8018eca4 | Name: gx_set_distort_scale | Date: 2026-09-06
// Set the global screen-distortion scale.
// ---------------------------------------------------------------------------
void gx_set_distort_scale(double v)
{
    FLOAT_806124ac = (float)v;
}

// ---------------------------------------------------------------------------
// Address: 0x8018ecac | Name: gx_tev_stage_screen_distort | Date: 2026-09-06
// Full-screen distortion TEV setup: binds the active material + noise texture
// 0x333, builds an rng-scrolled 2D tex-gen matrix, and programs two indirect
// tex-coord gens (scaled by FLOAT_806124ac / screen dimensions) into two TEV
// stages. Stage counters arrive via the spill frame.
// ---------------------------------------------------------------------------
void gx_tev_stage_screen_distort(int p1, int p2, unsigned int *pStage, double fpArg)
{
    (void)p1;
    (void)p2;
    (void)pStage;
    (void)fpArg;
    unsigned long long frame = __save_gpr_25();
    unsigned int stage0 = (unsigned int)((unsigned long long)frame >> 0x20);
    unsigned int stage1 = stage0 + 1;
    unsigned int *pColor = (unsigned int *)frame;

    // Copy 3 packed pairs from ROM 0x803cd0c8 onto a scratch header.
    float hdr[18];
    {
        unsigned int *dst = (unsigned int *)hdr - 1;
        unsigned int *src = (unsigned int *)0x803cd0c8;
        for (int i = 3; i != 0; i--) {
            unsigned int a = src[1];
            unsigned int b = src[2];
            src += 2;
            dst[1] = a;
            dst += 2;
            *dst = b;
        }
    }
    unsigned int colorStage0 = *pColor; *pColor = colorStage0 + 1;
    unsigned int colorStage1 = *pColor; *pColor = colorStage1 + 1;

    gx_cached_set_tev_order(colorStage0, 1, 4, 0x3c);
    FUN_803c9288(DAT_80612254, colorStage0);
    int tex = sprite_get_texture_id(0x333);
    FUN_803c9288((unsigned int *)(*DAT_80610a20 + tex * 0x58), colorStage1);

    unsigned int rngY = rng_counter_tick();
    double sy = (double)((float)((double)CONCAT44(0x43300000, rngY) - DOUBLE_80616898) /
                         FLOAT_80616890);
    unsigned int rngX = rng_counter_tick();
    double sx = (double)((float)((double)CONCAT44(0x43300000, rngX) - DOUBLE_80616898) /
                         FLOAT_80616890);

    float texMtx[16];
    FUN_803a1214(sx, sy, (double)FLOAT_80616878, texMtx);
    FUN_803cb2cc(texMtx, 0x2a, 1);
    gx_cached_set_tev_order(colorStage1, 1, 4, 0x2a);
    FUN_803c9ecc(1);
    FUN_803c9de0(0, colorStage1, colorStage1);
    FUN_803c9c9c(0, 0, 0);

    float coordA[16];
    coordA[2] = FLOAT_806124ac /
                (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80616888);
    FUN_803c9b24(1, coordA, '\x01');
    coordA[2] = FLOAT_80616878;
    coordA[3] = FLOAT_806124ac /
                (float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80616888);
    FUN_803c9b24(2, coordA, '\x01');

    FUN_803c9ab8(stage0, 0, 0, 3, 1, 0, 0, 0, 0, 0);
    FUN_803c9ab8(stage1, 0, 0, 3, 2, 0, 0, 0, 0, 0);
    gx_cached_set_tev_color_op(stage0, colorStage0, colorStage0, 4);
    gx_cached_set_tev_color_in(stage0, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_alpha_in(stage0, 7, 5, 4, 7);
    gx_cached_set_tev_color_env(stage0, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage0, 0, 0, 0, 1, 0);
    gx_cached_set_tev_color_op(stage1, colorStage0, colorStage0, 4);
    gx_cached_set_tev_color_in(stage1, 0xf, 10, 8, 0);
    gx_cached_set_tev_alpha_in(stage1, 7, 5, 4, 0);
    gx_cached_set_tev_color_env(stage1, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 0, 1, 0);
    __restore_gpr_25();
}

// ---- Additional raw symbols for the trailing two stages ----
extern "C" {
extern void gx_indirect_stage_set_hi(void);
extern unsigned int *particle_get_current_player_emitter(void);
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
}

// ---------------------------------------------------------------------------
// Address: 0x8018f024 | Name: gx_tev_stage_indirect_combine | Date: 2026-09-06
// Minimal indirect combine TEV stage: binds the active material and programs a
// single modulate stage, then enables the high indirect-stage bit. Colour/coord
// stage counters arrive via the spill frame and *pStage.
// ---------------------------------------------------------------------------
void gx_tev_stage_indirect_combine(int p1, int p2, unsigned int *pStage)
{
    (void)p1;
    (void)p2;
    unsigned long long frame = __save_gpr_28();
    unsigned int stage = (unsigned int)((unsigned long long)frame >> 0x20);
    unsigned int *pColor = (unsigned int *)frame;

    unsigned int colorStage = *pColor; *pColor = colorStage + 1;
    unsigned int coordStage = *pStage; *pStage = coordStage + 1;

    FUN_803c9288(DAT_80612254, colorStage);
    gx_cached_set_tev_color_op(stage, colorStage, coordStage, 4);
    gx_cached_set_tev_color_in(stage, 0xf, 0xf, 0xf, 8);
    gx_cached_set_tev_color_env(stage, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in(stage, 7, 5, 4, 7);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 1, 1, 0);
    gx_indirect_stage_set_hi();
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018f114 | Name: gx_tev_stage_distort_tex2a7 | Date: 2026-09-06
// Indirect-distortion TEV stage using distortion texture 0x2a7. Same shape as
// gx_tev_stage_distort_tex3b8/2a6 but the tex-coord gen scale is taken from the
// current player's particle emitter (fields [0x23]/[0x24]). ROM 0x803cd0e0.
// ---------------------------------------------------------------------------
void gx_tev_stage_distort_tex2a7(int p1, int p2, unsigned int *pStage)
{
    (void)p1;
    (void)p2;
    (void)pStage;
    unsigned long long frame = FUN_803826e4();
    unsigned int combineStage = (unsigned int)((unsigned long long)frame >> 0x20);
    unsigned int *pColor = (unsigned int *)frame;

    float mtx[6];
    {
        unsigned int *dst = (unsigned int *)mtx - 1;
        unsigned int *src = (unsigned int *)0x803cd0e0;
        for (int i = 3; i != 0; i--) {
            unsigned int a = src[1];
            unsigned int b = src[2];
            src += 2;
            dst[1] = a;
            dst += 2;
            *dst = b;
        }
    }
    unsigned int colorStage0 = *pColor; *pColor = colorStage0 + 1;
    unsigned int colorStage1 = *pColor; *pColor = colorStage1 + 1;

    unsigned int *emitter = particle_get_current_player_emitter();
    mtx[1] = (float)emitter[0x23];
    emitter = particle_get_current_player_emitter();
    mtx[2] = (float)emitter[0x24];

    gx_cached_set_tev_order(colorStage0, 1, 4, 0x3c);
    float scratchMtx[18];
    memset_wrapper((int)scratchMtx, 0, 0x30);
    FUN_803cb2cc(scratchMtx, 0x2a, 1);
    gx_cached_set_tev_order(colorStage1, 1, 4, 0x2a);
    FUN_803c9288(DAT_80612254, colorStage0);
    int tex = sprite_get_texture_id(0x2a7);
    FUN_803c9288((unsigned int *)(*DAT_80610a20 + tex * 0x58), colorStage1);
    FUN_803c9ecc(1);
    FUN_803c9de0(0, colorStage0, colorStage0);
    FUN_803c9c9c(0, 0, 0);
    FUN_803c9b24(1, mtx, '\x01');
    FUN_803c9ab8(combineStage, 0, 0, 0, 1, 0, 0, 0, 0, 0);
    gx_cached_set_tev_color_op(combineStage, colorStage1, colorStage1, 4);
    gx_cached_set_tev_color_in(combineStage, 0xf, 10, 8, 0xf);
    gx_cached_set_tev_alpha_in(combineStage, 7, 7, 7, 5);
    gx_cached_set_tev_color_env(combineStage, 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_env(combineStage, 0, 0, 1, 1, 0);
    FUN_80382730();
}
