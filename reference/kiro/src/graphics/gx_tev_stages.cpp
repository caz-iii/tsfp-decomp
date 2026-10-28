// gx_tev_stages.cpp
// Per-material-type TEV-stage builder callbacks. Each takes the next free TEV
// stage index and programs one (or more) TEV stage(s) for a specific material
// effect (konst grey, alpha-double, water-fog tint, zone-light additive,
// modulate pair, lit-quad), returning the next free stage. These are installed
// as active-material stage callbacks and are usually referenced by address.
//
// Batch: batch_495
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_set_blend_tag(int cat, int mask);
extern int  gx_get_state_c0(void);
extern void gx_invalidate_state_e4(void);
extern void gx_set_konst_alpha_sel(int sel);
extern void glow_rt_teardown(void);
extern int  particle_zone_light_get_state(int zoneId);
extern unsigned int color_scale_rgb(double scale, unsigned int rgb);

// GX konst / indirect / material-vertex builders.
extern void FUN_803ca174(int slot, unsigned int *rgba);   // set TEV konst colour
extern void FUN_803ca2f8(unsigned int stage, int a2);     // indirect on stage
extern void FUN_803c8874(int a1);                          // blend eq / combine op
extern void FUN_803c86a4(int a1, unsigned int *rgba);      // material colour A
extern void FUN_803c878c(int slot, unsigned int *rgba);    // material colour / konst
extern void FUN_803c8610(int rec, unsigned int *rgba);     // vtx colour
extern void FUN_803c84e4(double x, double y, double z, int rec);   // vtx pos
extern void FUN_803c8510(double x, double y, double z, int rec);   // vtx normal/2nd pos
extern void FUN_803c8404(double a, double b, double c, int rec);   // vtx uv/param
extern void FUN_803c8414(double a, double b, int rec, int a4);     // vtx uv2/param
extern void FUN_803c8628(int rec, int a2);                          // commit vtx record
extern void FUN_803c88b0(int a1, int a2, int a3, int a4, int a5, int a6, int a7);  // blend combine

// Frame-guard spill pair (returns packed stage in high word).
extern unsigned long long FUN_803826f0(void);
extern void               FUN_8038273c(void);

// ---- Data ----
extern unsigned int DAT_805acc00;  // grey-konst byte source (low)
extern unsigned int DAT_805acc04;  // grey-konst RGB source
extern int  *DAT_80611b24;         // camera/entity chain root
extern int   DAT_80611b14;         // highest valid zone id

extern float  FLOAT_80616874;
extern float  FLOAT_80616878;
extern float  FLOAT_8061687c;
extern float  FLOAT_8061686c;
extern float  FLOAT_80616880;      // water depth->fog scale
extern double DOUBLE_80616888;     // int->double magic bias
extern float  DAT_803cd078;        // lit-quad rodata consts
extern float  DAT_803cd07c;
extern float  DAT_803cd080;
extern float  DAT_803cd084;
extern float  DAT_803cd088;
extern float  DAT_803cd08c;
}

// water_surface_get_config_ptr returns a config record pointer.
extern "C" unsigned char *water_surface_get_config_ptr(void);

// ---------------------------------------------------------------------------
// Address: 0x8018dd58 | Name: gx_tev_stage_konst_grey_b | Date: 2026-09-06
// Program one TEV stage that modulates by a grey konst colour built from
// DAT_805acc00/04. Returns the next free stage.
// ---------------------------------------------------------------------------
int gx_tev_stage_konst_grey_b(unsigned int stage)
{
    unsigned int konst = ((DAT_805acc04 & 0xff) << 24) | (((DAT_805acc04 >> 8) & 0xff) << 16) |
                         (((DAT_805acc04 >> 16) & 0xff) << 8) | (DAT_805acc00 & 0xff);
    unsigned int sel = konst;
    FUN_803ca174(3, &sel);
    gx_cached_set_tev_color_op(stage, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(stage, 0, 6, 7, 0xf);
    gx_cached_set_tev_alpha_in(stage, 7, 7, 7, 0);
    gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 0, 1, 0);
    return (int)stage + 1;
}

// ---------------------------------------------------------------------------
// Address: 0x8018deac | Name: gx_tev_stage_lit_quad_setup | Date: 2026-09-06
// Set up an additive blend + build a lit material-vertex record (position,
// colour, normal, UVs) from the rodata constants at 0x803cd078, then program
// the blend combine. Returns its stage argument unchanged.
// ---------------------------------------------------------------------------
int gx_tev_stage_lit_quad_setup(int stage)
{
    float posX = DAT_803cd078, posY = DAT_803cd07c, posZ = DAT_803cd080;
    float nX = DAT_803cd084, nY = DAT_803cd088, nZ = DAT_803cd08c;
    unsigned char rec[72];

    gx_set_blend_tag(3, 1);
    FUN_803c8874(1);
    unsigned int colA = 0;
    FUN_803c86a4(2, &colA);
    unsigned int colB = 0xff;
    FUN_803c878c(2, &colB);
    unsigned int colC = 0xff;
    FUN_803c8610((int)rec, &colC);
    FUN_803c84e4((double)posX, (double)posY, (double)posZ, (int)rec);
    FUN_803c8510((double)nX, (double)nY, (double)nZ, (int)rec);
    FUN_803c8404((double)FLOAT_80616874, (double)FLOAT_80616878, (double)FLOAT_80616878, (int)rec);
    FUN_803c8414((double)FLOAT_8061687c, (double)FLOAT_8061686c, (int)rec, 3);
    FUN_803c8628((int)rec, 1);
    FUN_803c88b0(2, 1, 0, 0, 1, 2, 1);
    return stage;
}

// ---------------------------------------------------------------------------
// Address: 0x8018e008 | Name: gx_glow_teardown_thunk | Date: 2026-09-06
// Thunk to glow_rt_teardown (glow render-target reset).
// ---------------------------------------------------------------------------
void gx_glow_teardown_thunk(void)
{
    glow_rt_teardown();
}

// ---------------------------------------------------------------------------
// Address: 0x8018e028 | Name: gx_tev_stage_alpha_double | Date: 2026-09-06
// Program one TEV stage whose konst alpha is the current material state-c0
// value doubled (saturated). Stage index arrives packed in the spill frame.
// ---------------------------------------------------------------------------
void gx_tev_stage_alpha_double(void)
{
    unsigned int stage = (unsigned int)(FUN_803826f0() >> 0x20);
    unsigned int c0 = (unsigned int)gx_get_state_c0();
    gx_invalidate_state_e4();
    unsigned int doubled = (c0 & 0xff) << 1;
    unsigned char lo = 0xff;
    if (doubled < 0xff) {
        lo = (unsigned char)doubled;
    }
    unsigned int konst = (c0 & 0xffffff00) | lo;
    unsigned int sel = konst;
    FUN_803ca174(1, &sel);
    gx_cached_set_tev_color_op(stage, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(stage, 0xf, 0xf, 0xf, 0);
    gx_cached_set_tev_alpha_in(stage, 7, 0, 1, 7);
    gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 0, 1, 0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018e130 | Name: gx_tev_stage_water_fog | Date: 2026-09-06
// Program the water-fog tint stage: derive fog colour/alpha from the water
// config depth (config+0x28) and colour (config+4), set the material colour and
// konst-alpha selector. Returns its stage argument unchanged.
// ---------------------------------------------------------------------------
int gx_tev_stage_water_fog(int stage)
{
    unsigned char *cfg = water_surface_get_config_ptr();
    unsigned int depth = *(unsigned int *)(cfg + 0x28);
    float depthF = FLOAT_80616880 *
                   (float)((double)CONCAT44(0x43300000, depth ^ 0x80000000) - DOUBLE_80616888);
    unsigned int tint = color_scale_rgb((double)depthF, *(unsigned int *)(cfg + 4));
    char fogA = (char)(-1 - (int)(((unsigned char)(depth << 1)) | ((unsigned char)((int)depth >> 6))));

    unsigned int matCol = ((unsigned int)(unsigned char)fogA << 24) |
                          ((unsigned int)(unsigned char)fogA << 16) |
                          ((unsigned int)(unsigned char)fogA << 8) | 0xff;
    unsigned int konst = (tint & 0xffffff00) | 0xff;

    gx_set_blend_tag(2, 0);
    unsigned int matSel = matCol;
    FUN_803c878c(4, &matSel);
    gx_set_konst_alpha_sel(6);
    unsigned int konstSel = konst;
    FUN_803ca174(3, &konstSel);
    return stage;
}

// ---------------------------------------------------------------------------
// Address: 0x8018e214 | Name: gx_set_konst_alpha_sel_default | Date: 2026-09-06
// Restore the default (0xf) konst-alpha selector.
// ---------------------------------------------------------------------------
void gx_set_konst_alpha_sel_default(void)
{
    gx_set_konst_alpha_sel(0xf);
}

// ---------------------------------------------------------------------------
// Address: 0x8018e238 | Name: gx_tev_stage_zone_light | Date: 2026-09-06
// When the current entity's zone is valid, program one TEV stage that adds the
// zone's combined ambient + point light colour (saturated) as a konst. Returns
// stage+1 if programmed, else the stage unchanged.
// ---------------------------------------------------------------------------
unsigned int gx_tev_stage_zone_light(unsigned int stage)
{
    unsigned int outStage = stage;
    if (DAT_80611b24 != 0 && *DAT_80611b24 != 0) {
        int zoneId = *(int *)(*DAT_80611b24 + 0x34);
        if (zoneId <= DAT_80611b14) {
            outStage = stage + 1;
            int zl = particle_zone_light_get_state(zoneId);
            unsigned int pt = *(unsigned int *)(zl + 0x1c);
            unsigned int amb = *(unsigned int *)(zl + 0x10);

            unsigned int r = (pt & 0xff) + (amb & 0xff);
            if (r > 0xff) r = 0xff;
            unsigned int g = ((pt >> 8) & 0xff) + ((amb >> 8) & 0xff);
            if (g > 0xff) g = 0xff;
            unsigned int b = ((pt >> 16) & 0xff) + ((amb >> 16) & 0xff);
            if (b > 0xff) b = 0xff;
            unsigned int a = (pt >> 24) + (amb >> 24);
            if (a > 0xff) a = 0xff;

            unsigned int konst = (b << 24) | (g << 16) | (r << 8) | a;
            unsigned int sel = konst;
            FUN_803ca174(3, &sel);
            gx_cached_set_tev_color_op(stage, 0xff, 0xff, 0xff);
            gx_cached_set_tev_color_in(stage, 0xf, 6, 0, 0xf);
            gx_cached_set_tev_alpha_in(stage, 7, 3, 0, 7);
            gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
            gx_cached_set_tev_alpha_env(stage, 0, 0, 0, 1, 0);
        }
    }
    return outStage;
}

// ---------------------------------------------------------------------------
// Address: 0x8018e3bc | Name: gx_tev_stage_pair_modulate | Date: 2026-09-06
// Program a pair of modulate TEV stages (indirect-on) for a two-layer material.
// Returns stage+2.
// ---------------------------------------------------------------------------
int gx_tev_stage_pair_modulate(unsigned int stage)
{
    FUN_803ca2f8(stage, 0);
    gx_cached_set_tev_color_op(stage, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(stage, 0xf, 0xf, 0xf, 0);
    gx_cached_set_tev_alpha_in(stage, 7, 1, 6, 0);
    gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage, 1, 0, 0, 1, 0);

    FUN_803ca2f8(stage + 1, 0);
    gx_cached_set_tev_color_op(stage + 1, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(stage + 1, 0xf, 0xf, 0xf, 0);
    gx_cached_set_tev_alpha_in(stage + 1, 7, 0, 6, 1);
    gx_cached_set_tev_color_env(stage + 1, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage + 1, 0, 0, 0, 1, 0);
    return (int)stage + 2;
}
