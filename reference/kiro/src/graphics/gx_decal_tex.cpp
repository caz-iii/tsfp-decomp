// gx_decal_tex.cpp
// Decal-group mask-texture allocator. Builds the 64x64 8-bit intensity mask
// texture a decal group renders through, allocating the pixel buffer, filling
// it to 0x7f (mid grey), and binding it as a GX texture object in the group's
// render context.
//
// Batch: batch_493
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// GX texture size query + descriptor build + LOD/filter config.
extern unsigned int FUN_803c8960(int w, int h, int fmt, char c, int mip);
extern void FUN_803c8b84(unsigned int *texObj, unsigned int data, int w, int h, int fmt,
                         int wrapS, int wrapT, char mip);
extern void FUN_803c8e18(double lodBias, double minLod, double maxLod, unsigned int *texObj,
                         int a5, int a6, int a7, char a8, int a9);

extern void memset_wrapper(int dst, int val, int len);

extern float FLOAT_80616840;  // decal-mask LOD parameter / base UV origin

// --- batch_494 decal-render helpers (per-file externs) ---
// Sprite/material + GX state helpers (declared void where the ROM ignores the
// return; the ones whose r3 is consumed are typed to return int).
extern int  sprite_get_texture_id(unsigned int spriteId);
extern int  gx_set_blend_mode(int mode);
extern void gx_set_color_update_mode(void);
extern int  gx_set_sub_material_id(int id);
extern int  gx_set_num_chans(int n);
extern void gx_set_blend_tag(int cat, int mask);
extern void gx_apply_fog_params(void);
extern void gx_reset_indirect_stages(void);
extern void gx_bind_material_fallback(void);
extern void gx_draw_sprite_quad(double x0, double y0, double x1, double y1, int u0, int v0, int u1,
                                int v1);
extern double game_time_to_frames(void);
extern void gx_indirect_stage_config(void);
extern void gx_indirect_matrix_update(int slot, unsigned int value, int mode);
extern void gx_load_batch_pos_matrix(unsigned int *desc);
extern int  gx_set_material_override_cb(int cb);
extern void gx_invalidate_fog_params(void);
extern void gx_cached_set_tev_order(int stage, int texMap, int texCoord, int channel);
extern void gx_cached_set_tev_order_ex(int stage, int a2, int a3, int a4, int a5, int a6);
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);

// 4x4 matrix helpers.
extern void mtx4_set_translation(double x, double y, double z, int outMtx);
extern void mtx4_multiply(float *a, float *b, float *out);
extern void mtx4_transpose(float *src, float *dst);
extern void mtx4_set_identity(int outMtx);
extern void mtx4_scale_axes(double sx, double sy, double sz, float *outMtx);
extern void mtx4_write_translation(double tx, double ty, double tz, int outMtx);

// EFB copy / GX tex-matrix load / indirect-stage helpers.
extern void FUN_803c78c8(int x, int y, int w, int h);       // set EFB copy source rect
extern void FUN_803c7978(int w, int h, int fmt, int a4);    // set EFB copy dest format
extern void FUN_803c8224(unsigned int buf, int a2);         // EFB copy to texture buffer
extern void FUN_803c7080(void);                             // finalize EFB copy
extern void FUN_803cb2cc(float *mtx, int slot, int a3);     // GXLoadTexMtxImm
extern void FUN_803c9ecc(int n);                            // indirect: set num stages
extern void FUN_803c9de0(int a1, int a2, int a3);           // indirect stage tex config
extern void FUN_803c9c9c(int a1, int a2, int a3);           // indirect stage coord config
extern void FUN_803c9ab8(unsigned int stage, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                         int a9, int a10);                  // indirect stage full config
extern void FUN_803ca2f8(unsigned int stage, int a2);       // indirect on stage
extern void FUN_803c9b24(int coord, float *mtx, char a3);   // program tex-coord gen
extern void FUN_803c9ef0(int stage);                        // indirect stage disable
extern void FUN_803c8f8c(unsigned int *mat, int a2, int a3);// material tex select
extern void FUN_803c9288(unsigned int *tex, int slot);      // bind tex to slot

extern int *DAT_80610a20;    // active render-target group table ([0]=slot base,[2]=lo,[3]=hi)
extern unsigned int *DAT_80612254;  // active material pointer
extern int   DAT_80612250;   // env-tex-matrix dirty flag
extern int   DAT_80612258;   // reflect-matrix latch
extern int   DAT_80611d54;   // active render context (+0x208 view mtx)
extern float DAT_8058e3cc;   // env-tex reset matrix

extern float FLOAT_80616844; // decal mask half UV size
extern float FLOAT_80616848; // overlay A scroll rate
extern float FLOAT_8061684c; // overlay B scroll rate
extern float FLOAT_80616850; // projection Y bias
extern float FLOAT_80616854; // tex-gen scale A
extern float FLOAT_80616858; // tex-gen scale B x
extern float FLOAT_8061685c; // tex-gen scale/translate B
extern float FLOAT_80616860; // tex-gen translate B z

// Spawn-frame register accessors (return a context pointer / packed pair).
extern int  FUN_803826d8(void);
extern void FUN_80382724(void);
extern int  FUN_803826e8(void);
extern void FUN_80382734(void);
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
}

// mem_alloc_aligned in the shared decls is void-return, but the ROM consumes
// its r3 block pointer here. Call it through a typed function-pointer alias so
// we recover the return without a conflicting redeclaration.
extern "C" void mem_alloc_aligned(unsigned long long a1, unsigned long long a2,
                                  unsigned long long a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6,
                                  unsigned long long a7, unsigned long long a8,
                                  unsigned int align, unsigned int size, int type,
                                  int a12, int a13, int a14, int a15, int a16);
typedef unsigned int (*MemAllocAlignedFn)(unsigned long long, unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long, unsigned int,
                                          unsigned int, int, int, int, int, int, int);
#define MEM_ALLOC_ALIGNED ((MemAllocAlignedFn)mem_alloc_aligned)

// ---------------------------------------------------------------------------
// Address: 0x8018cbd0 | Name: decal_group_alloc_mask_tex | Date: 2026-09-06
// Allocate and bind a 64x64 8-bit-intensity mask texture for a decal group.
// (The leading 8 args are FP/GPR ABI passthrough into the aligned allocator.)
// group+0x10c4 holds the pixel buffer; group+0x10a4 holds the GX texture object.
// Caller: decal_group_find_or_create.
// ---------------------------------------------------------------------------
void decal_group_alloc_mask_tex(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8,
                                unsigned int param_9, int group)
{
    (void)param_9;
    unsigned int texSize = FUN_803c8960(0x40, 0x40, 1, '\0', 0);
    unsigned int buf = MEM_ALLOC_ALIGNED(param_1, param_2, param_3, param_4, param_5, param_6,
                                         param_7, param_8, 0x20, texSize, 1, 0, 0, 0, 0, 0);
    *(unsigned int *)(group + 0x10c4) = buf;
    memset_wrapper((int)*(unsigned int *)(group + 0x10c4), 0x7f, texSize);
    FUN_803c8b84((unsigned int *)(group + 0x10a4), *(unsigned int *)(group + 0x10c4), 0x40, 0x40, 1,
                 1, 1, '\0');
    double lod = (double)FLOAT_80616840;
    FUN_803c8e18(lod, lod, lod, (unsigned int *)(group + 0x10a4), 1, 1, 0, '\0', 0);
}

// ===========================================================================
// batch_494: decal render passes (mask compose + projection + TEV shader)
// ===========================================================================

// The projected-decal TEV shader is installed as the active-material override
// callback; it is referenced by address (-0x7fe72ed8 == 0x8018d128).
extern "C" int decal_tev_setup(int stage, int *pCoord, int *pStage);

// ---------------------------------------------------------------------------
// Address: 0x8018cc90 | Name: decal_mask_render_pass | Date: 2026-09-06
// Compose the decal mask texture into the 64x64 render target: draw the base
// sprite (0x360) plus two time-scrolling overlay layers, then EFB-copy the
// result into the group's mask buffer (frame+0x10c4). Caller: decal_tick_uv_scroll.
// ---------------------------------------------------------------------------
void decal_mask_render_pass(void)
{
    unsigned long long frame = __save_gpr_28();
    int frameBase = (int)frame;

    int baseTex = sprite_get_texture_id(0x360);
    int slotBase = *DAT_80610a20;
    int savedBlend = gx_set_blend_mode(0);
    gx_set_color_update_mode();
    gx_set_sub_material_id(0);
    int savedChans = gx_set_num_chans(0);

    // Copy the 4 packed UV/colour pairs from the group scratch (frame+0x10a0)
    // into the base material record (slotBase + baseTex*0x58 - 4).
    unsigned int *dst = (unsigned int *)(slotBase + baseTex * 0x58 - 4);
    unsigned int *src = (unsigned int *)(frameBase + 0x10a0);
    for (int i = 4; i != 0; i--) {
        unsigned int a = src[1];
        unsigned int b = src[2];
        src += 2;
        dst[1] = a;
        dst += 2;
        *dst = b;
    }

    gx_set_blend_tag(0, 0);
    gx_apply_fog_params();
    gx_reset_indirect_stages();

    int tex = sprite_get_texture_id(0);
    if (tex < 0 || DAT_80610a20[3] <= tex || tex <= DAT_80610a20[2]) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = (unsigned int *)(*DAT_80610a20 + tex * 0x58);
    }
    gx_draw_sprite_quad((double)FLOAT_80616840, (double)FLOAT_80616840, (double)FLOAT_80616844,
                        (double)FLOAT_80616844, 0, 0, 0x400, 0x400);

    // Scrolling overlay A (vertical scroll).
    gx_set_sub_material_id(1);
    tex = sprite_get_texture_id(0x361);
    if (tex < 0 || DAT_80610a20[3] <= tex || tex <= DAT_80610a20[2]) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = (unsigned int *)(*DAT_80610a20 + tex * 0x58);
    }
    double t = game_time_to_frames();
    double half = (double)FLOAT_80616844;
    int scrollA = (int)(half * (double)(float)((double)FLOAT_80616848 * t));
    gx_draw_sprite_quad((double)FLOAT_80616840, (double)FLOAT_80616840, half, half, 0, scrollA << 4,
                        0x400, (scrollA + 0x40) * 0x10);

    // Scrolling overlay B (horizontal scroll).
    t = game_time_to_frames();
    half = (double)FLOAT_80616844;
    int scrollB = (int)(half * (double)(float)((double)FLOAT_8061684c * t));
    gx_draw_sprite_quad((double)FLOAT_80616840, (double)FLOAT_80616840, half, half, scrollB << 4, 0,
                        (scrollB + 0x40) * 0x10, 0x400);

    // EFB copy the composed mask into the group buffer.
    gx_set_sub_material_id(0xe);
    FUN_803c78c8(0, 0, 0x40, 0x40);
    FUN_803c7978(0x40, 0x40, 0x28, 0);
    FUN_803c8224(*(unsigned int *)(frameBase + 0x10c4), 0);
    FUN_803c7080();

    gx_set_blend_mode(savedBlend);
    gx_set_num_chans(savedChans);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018cefc | Name: decal_projection_render_pass | Date: 2026-09-06
// Project the decal onto the scene: build the projection matrix (from the
// context origin or its linked body), program the indirect-TEV env-tex matrix,
// and submit each decal strip's display list. Caller: decal_render_dispatch.
// ---------------------------------------------------------------------------
void decal_projection_render_pass(void)
{
    int ctx = FUN_803826d8();
    int savedBlend = gx_set_blend_mode(4);
    int savedSub = gx_set_sub_material_id(1);
    gx_set_color_update_mode();

    float projMtx[16];
    int linked = *(int *)(ctx + 0x28);
    if (linked == 0) {
        mtx4_set_translation((double)*(float *)(ctx + 8), (double)*(float *)(ctx + 0xc),
                             (double)*(float *)(ctx + 0x10), (int)projMtx);
    } else {
        int body = *(int *)(*(int *)(linked + 4) + 4);
        mtx4_set_translation((double)(*(float *)(linked + 100) + *(float *)(body + 0x2c)),
                             (double)(*(float *)(linked + 0x68) + *(float *)(body + 0x30) +
                                      FLOAT_80616850),
                             (double)(*(float *)(linked + 0x6c) + *(float *)(body + 0x34)),
                             (int)projMtx);
    }
    mtx4_multiply(projMtx, *(float **)(DAT_80611d54 + 0x208), projMtx);
    mtx4_transpose(projMtx, &DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;

    unsigned int savedChans = gx_set_num_chans(0);
    gx_indirect_stage_config();
    gx_cached_set_tev_order(0, 1, 0xc, 0x3c);

    int count = *(int *)(ctx + 0x2c);
    int off = 0;
    for (int i = 0; i < count; i++, off += 4) {
        int strip = *(int *)(ctx + off + 0x58);
        int linkFlags = *(int *)(*(int *)(*(int *)(ctx + 0x28) + 4) + 4) + 0xc;
        if ((*(unsigned int *)linkFlags & 0x20) == 0) {
            gx_indirect_matrix_update(0xd, *(unsigned int *)(strip + 8), 8);
        } else {
            gx_indirect_matrix_update(0xd, *(unsigned int *)(strip + 8), 4);
        }
        if (*(int *)(ctx + 0x28) == 0 || (*(unsigned int *)linkFlags & 0x10) == 0) {
            gx_indirect_matrix_update(9, *(unsigned int *)(strip + 4), 0xc);
        } else {
            gx_indirect_matrix_update(9, *(unsigned int *)(strip + 4), 6);
        }
        gx_indirect_matrix_update(0xb, *(unsigned int *)(strip + 0x10), 4);
        unsigned int *desc = *(unsigned int **)(ctx + off + 0x80);
        if (desc[1] != 0) {
            gx_load_batch_pos_matrix(desc);
        }
    }

    gx_set_num_chans(savedChans);
    gx_set_blend_mode(savedBlend);
    gx_set_sub_material_id(savedSub);
    gx_set_color_update_mode();
    mtx4_transpose(*(float **)(DAT_80611d54 + 0x208), &DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
    FUN_80382724();
}

// ---------------------------------------------------------------------------
// Address: 0x8018d128 | Name: decal_tev_setup | Date: 2026-09-06
// Active-material override callback for the projected-decal shader: build the
// two tex-gen scale/translate matrices, load them into GX tex-mtx slots, and
// program the 3-stage indirect TEV combine. Returns the next free TEV stage.
// ---------------------------------------------------------------------------
int decal_tev_setup(int stage, int *pCoord, int *pStage)
{
    *pStage = *pStage + 1;

    // Copy 3 packed pairs from ROM (0x803cd044) into a scratch matrix header.
    float hdr[6];
    {
        unsigned int *dst = (unsigned int *)hdr - 1;
        unsigned int *src = (unsigned int *)0x803cd044;
        for (int i = 3; i != 0; i--) {
            unsigned int a = src[1];
            unsigned int b = src[2];
            src += 2;
            dst[1] = a;
            dst += 2;
            *dst = b;
        }
    }
    *pCoord = *pCoord + 2;
    gx_set_blend_tag(0, 0);

    float mtxA[16];
    mtx4_set_identity((int)mtxA);
    mtx4_scale_axes((double)FLOAT_80616854, (double)FLOAT_80616854, (double)FLOAT_80616840, mtxA);
    // (translate row seeded from FLOAT_80616840 by the ROM prologue)

    float mtxB[16];
    mtx4_set_identity((int)mtxB);
    mtx4_scale_axes((double)FLOAT_80616858, (double)FLOAT_8061685c, (double)FLOAT_80616840, mtxB);
    mtx4_write_translation((double)FLOAT_8061685c, (double)FLOAT_8061685c, (double)FLOAT_80616860,
                           (int)mtxB);

    float mtxAt[16];
    float mtxBt[16];
    mtx4_transpose(mtxA, mtxAt);
    mtx4_transpose(mtxB, mtxBt);
    FUN_803cb2cc(mtxAt, 0x21, 0);
    FUN_803cb2cc(mtxBt, 0x43, 0);
    gx_cached_set_tev_order_ex(1, 0, 0, 0x21, 0, 0x43);

    FUN_803c9ecc(2);
    FUN_803c9de0(0, 0, 1);
    FUN_803c9c9c(0, 1, 1);
    FUN_803c9de0(1, 0, 1);
    FUN_803c9c9c(1, 1, 1);
    FUN_803c9ab8((unsigned int)(stage + 1), 0, 0, 3, 3, 0, 0, 0, 0, 0);
    FUN_803c9ab8((unsigned int)(stage + 2), 1, 0, 0, 2, 0, 0, 1, 0, 0);
    FUN_803ca2f8((unsigned int)stage, 1);

    gx_cached_set_tev_color_op((unsigned int)stage, 1, 2, 4);
    gx_cached_set_tev_color_in((unsigned int)stage, 0xf, 0xf, 0xf, 8);
    gx_cached_set_tev_color_env((unsigned int)stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_in((unsigned int)stage, 7, 7, 7, 6);
    gx_cached_set_tev_alpha_env((unsigned int)stage, 0, 0, 0, 1, 0);

    gx_cached_set_tev_color_op((unsigned int)(stage + 1), 0, 0, 0xff);
    gx_cached_set_tev_color_in((unsigned int)(stage + 1), 0xf, 0, 8, 0xf);
    gx_cached_set_tev_color_env((unsigned int)(stage + 1), 0, 0, 1, 1, 0);
    gx_cached_set_tev_alpha_in((unsigned int)(stage + 1), 7, 7, 7, 0);
    gx_cached_set_tev_alpha_env((unsigned int)(stage + 1), 0, 0, 0, 1, 0);

    gx_cached_set_tev_color_op((unsigned int)(stage + 2), 0, 0, 0xff);
    gx_cached_set_tev_color_in((unsigned int)(stage + 2), 0xf, 0, 8, 0xf);
    gx_cached_set_tev_color_env((unsigned int)(stage + 2), 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_in((unsigned int)(stage + 2), 7, 7, 7, 0);
    gx_cached_set_tev_alpha_env((unsigned int)(stage + 2), 0, 0, 0, 1, 0);

    FUN_803c9b24(2, hdr, '\x01');
    FUN_803c9b24(3, hdr, '\x01');
    return stage + 3;
}

// ---------------------------------------------------------------------------
// Address: 0x8018d4a8 | Name: decal_tev_teardown | Date: 2026-09-06
// Tear down the projected-decal TEV state: invalidate fog and disable the
// three indirect stages.
// ---------------------------------------------------------------------------
void decal_tev_teardown(void)
{
    gx_invalidate_fog_params();
    FUN_803c9ecc(0);
    FUN_803c9ef0(0);
    FUN_803c9ef0(1);
    FUN_803c9ef0(2);
}

// ---------------------------------------------------------------------------
// Address: 0x8018d4ec | Name: decal_render_dispatch | Date: 2026-09-06
// Top-level projected-decal render: bind the base sprite (0x35f) as the active
// material, install the decal TEV setup callback, bind the mask/projection/
// pattern textures, run the projection pass, then tear down. Caller:
// decal_render_instance.
// ---------------------------------------------------------------------------
void decal_render_dispatch(void)
{
    int ctx = FUN_803826e8();
    int group = *(int *)(ctx + 0xbc);
    int patTex = sprite_get_texture_id(0x32b);
    int slotBase = *DAT_80610a20;
    int baseTex = sprite_get_texture_id(0x35f);
    sprite_get_texture_id(0x360);
    gx_set_blend_tag(0, 0);

    if (baseTex < 0 || DAT_80610a20[3] <= baseTex || baseTex <= DAT_80610a20[2]) {
        gx_bind_material_fallback();
    } else {
        DAT_80612254 = (unsigned int *)(*DAT_80610a20 + baseTex * 0x58);
    }
    int savedCb = gx_set_material_override_cb((int)(long)&decal_tev_setup);
    gx_apply_fog_params();
    gx_reset_indirect_stages();
    FUN_803c8f8c(DAT_80612254, 1, 1);
    FUN_803c9288(DAT_80612254, 0);
    FUN_803c9288((unsigned int *)(group + 0x10a4), 1);
    FUN_803c9288((unsigned int *)(slotBase + patTex * 0x58), 2);
    decal_projection_render_pass();
    decal_tev_teardown();
    gx_set_material_override_cb(savedCb);
    FUN_80382734();
}
