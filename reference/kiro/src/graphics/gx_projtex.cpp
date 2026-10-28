// gx_projtex.cpp
// Projected-texture / spotlight-cookie shader setup. Builds the projection and
// tex-gen matrices for a projected texture (light cookie, decal projector,
// gobo) driven by the config globals at 0x805aca68.. and programs the TEV
// stages. These are installed as active-material override callbacks (referenced
// by address) rather than called directly.
//
// Batch: batch_494
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
extern int  sprite_get_texture_id(unsigned int spriteId);
extern void gx_mark_material_dirty(void);

// TEV / tex-gen / konst helpers.
extern void gx_cached_set_tev_order_ex(unsigned int stage, int a2, int a3, int a4, int a5, int a6);
extern void gx_cached_set_tev_color_op(unsigned int stage, int a2, int a3, int a4);
extern void gx_cached_set_tev_color_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_color_env(unsigned int stage, int a, int b, int c, int d, int e);
extern void gx_cached_set_tev_alpha_in(unsigned int stage, int a, int b, int c, int d);
extern void gx_cached_set_tev_alpha_env(unsigned int stage, int a, int b, int c, int d, int e);

extern void FUN_803c9288(unsigned int *tex, int slot);      // bind tex to slot
extern void FUN_803ca238(int slot, unsigned int *rgba);     // set TEV konst colour (variant)
extern void FUN_803ca174(int slot, unsigned int *rgba);     // set TEV konst colour
extern void FUN_803ca29c(unsigned int stage, int sel);      // set konst colour selector
extern void FUN_803cb2cc(float *mtx, int slot, int a3);     // GXLoadTexMtxImm
extern void FUN_803a12c8(double a, double b, double c, double d, double e, double f, double g,
                         double h, float *outMtx);           // perspective/frustum matrix build

extern void mtx4_set_rotation_zyx(double rz, double ry, double rx, float *outMtx);
extern void mtx4_multiply(float *a, float *b, float *out);
extern void mtx4_transpose(float *src, float *dst);
extern void mtx4_set_identity(int outMtx);
extern void mtx4_copy(float *src, float *dst);

// Config globals for the projected texture.
extern int   DAT_806124a0;    // material dirty-check mode
extern int   DAT_806124a4;    // projection-matrix source flag (0 = entity chain, else view)
extern int   DAT_80611d54;    // active render context (+0x210 alt view mtx)
extern int  *DAT_80611b24;    // camera/entity chain root
extern int   DAT_80611b28;    // bone index into the chain matrix palette
extern int  *DAT_80610a20;    // active render-target group table ([0]=slot base)

extern float DAT_805aca68;    // projector pos X
extern float DAT_805aca6c;    // projector pos Y
extern float DAT_805aca70;    // projector pos Z
extern float DAT_805aca74;    // projector rot Z
extern float DAT_805aca78;    // projector rot Y
extern float DAT_805aca7c;    // projector rot X
extern unsigned char DAT_805aca80;  // colour R
extern unsigned char DAT_805aca84;  // colour G
extern unsigned char DAT_805aca88;  // colour B
extern unsigned char DAT_805aca8c;  // colour A
extern unsigned int  DAT_805aca90;  // sprite/texture id
extern unsigned char DAT_805aca94;  // overall alpha
extern unsigned char DAT_805acab0;  // konst grey level

extern float FLOAT_80616868; // projection FOV/scale
extern float FLOAT_8061686c; // projection aspect/param
extern float FLOAT_80616870; // projection near/depth param

extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
}

// ---------------------------------------------------------------------------
// Address: 0x8018d7ac | Name: gx_mark_dirty_if_mode | Date: 2026-09-06
// Mark the active material dirty when the current material mode requires a
// re-flush (modes 1 and 8).
// ---------------------------------------------------------------------------
void gx_mark_dirty_if_mode(void)
{
    if (DAT_806124a0 == 1 || DAT_806124a0 == 8) {
        gx_mark_material_dirty();
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018d7e0 | Name: projtex_cookie_tev_setup | Date: 2026-09-06
// Projected-texture / spotlight-cookie shader setup callback. Binds the cookie
// texture, programs its konst colour + alpha, builds the projection matrix
// (rotation ZYX + perspective) and the world->projector matrix (from the entity
// chain or the inverse view), loads them into GX tex-mtx slots, and programs
// the two TEV stages. posX/posY/posZ arrive in the FP argument registers.
// ---------------------------------------------------------------------------
void projtex_cookie_tev_setup(int p1, int p2, unsigned int *pStage, double posX, double posY,
                              double posZ)
{
    (void)p1;
    (void)p2;
    unsigned long long frame = __save_gpr_28();
    unsigned int stage = (unsigned int)((unsigned long long)frame >> 0x20);
    unsigned int *outHi = (unsigned int *)((unsigned long long)frame >> 0x20);
    unsigned int *outLo = (unsigned int *)frame;

    unsigned int stage0 = *pStage;
    *pStage = stage0 + 1;
    unsigned int stage1 = stage + 1;
    unsigned int stageTex = *outLo;
    *outLo = stageTex + 1;

    int tex = sprite_get_texture_id(DAT_805aca90);
    double px = (double)DAT_805aca68;
    double py = (double)DAT_805aca6c;
    double pz = (double)DAT_805aca70;

    // pos arguments (FP regs) are only referenced by the ROM prologue spill;
    // acknowledge them so they are not dropped as dead.
    (void)posX;
    (void)posY;
    (void)posZ;

    // Konst colour (RGB) + selector.
    unsigned int col = ((unsigned int)DAT_805aca80 << 24) | ((unsigned int)DAT_805aca84 << 16) |
                       ((unsigned int)DAT_805aca88 << 8) | (unsigned int)DAT_805aca8c;
    FUN_803c9288((unsigned int *)(*DAT_80610a20 + tex * 0x58), stage0);
    unsigned int colKonst = col;
    FUN_803ca238(3, &colKonst);

    // Overall alpha konst (broadcast the alpha byte).
    unsigned int alphaKonst = (unsigned int)(((unsigned int)DAT_805aca94 << 8) |
                                             ((unsigned int)DAT_805aca94 << 16) |
                                             ((unsigned int)DAT_805aca94 << 24)) << 8;
    unsigned int alphaSel = alphaKonst;
    FUN_803ca174(2, &alphaSel);
    FUN_803ca29c(stage, 0xf);

    // Projection matrix: rotation (ZYX) composed with a perspective/frustum.
    float rotMtx[16];
    mtx4_set_rotation_zyx((double)DAT_805aca74, (double)DAT_805aca78, (double)DAT_805aca7c, rotMtx);
    float perspMtx[16];
    double aspect = (double)FLOAT_8061686c;
    FUN_803a12c8((double)FLOAT_80616868, aspect, (double)FLOAT_80616868, aspect,
                 (double)FLOAT_80616870, aspect, aspect, aspect, perspMtx);
    mtx4_multiply(perspMtx, rotMtx, rotMtx);
    FUN_803cb2cc(rotMtx, 0x40, 0);

    // World->projector matrix (slot 0x30).
    float worldMtx[16];
    mtx4_set_identity((int)worldMtx);
    if (DAT_806124a4 == 0) {
        if (*(int *)(*DAT_80611b24 + 0x20) == 2) {
            int body = DAT_80611b24[1];
            ((float *)worldMtx)[7]  += (float)((double)*(float *)(body + 0x2c) - px);
            ((float *)worldMtx)[11] += (float)((double)*(float *)(body + 0x30) - py);
            ((float *)worldMtx)[15] += (float)((double)*(float *)(body + 0x34) - pz);
        } else {
            float offMtx[16];
            mtx4_set_identity((int)offMtx);
            int body = DAT_80611b24[1];
            ((float *)offMtx)[7]  += *(float *)(body + 0x2c);
            ((float *)offMtx)[11] += *(float *)(body + 0x30);
            ((float *)offMtx)[15] += *(float *)(body + 0x34);

            float boneMtx[16];
            mtx4_set_identity((int)boneMtx);
            if (DAT_80611b24[4] != 0) {
                float tmp[16];
                mtx4_copy((float *)(DAT_80611b24[4] + DAT_80611b28 * 0x40), tmp);
                mtx4_transpose(tmp, boneMtx);
            }

            float rootMtx[16];
            mtx4_set_identity((int)rootMtx);
            int root = *DAT_80611b24;
            ((float *)rootMtx)[7]  += (float)((double)*(float *)(root + 100) - px);
            ((float *)rootMtx)[11] += (float)((double)*(float *)(root + 0x68) - py);
            ((float *)rootMtx)[15] += (float)((double)*(float *)(root + 0x6c) - pz);

            mtx4_multiply(offMtx, worldMtx, worldMtx);
            mtx4_multiply(boneMtx, worldMtx, worldMtx);
            mtx4_multiply(rootMtx, worldMtx, worldMtx);
        }
    } else {
        float viewMtx[16];
        mtx4_transpose(*(float **)(DAT_80611d54 + 0x210), viewMtx);
        ((float *)worldMtx)[7]  = (float)-px;
        ((float *)worldMtx)[11] = (float)-py;
        ((float *)worldMtx)[15] = (float)-pz;
        mtx4_multiply(viewMtx, worldMtx, worldMtx);
        DAT_806124a4 = 0;
    }
    FUN_803cb2cc(worldMtx, 0x30, 0);

    // TEV stages.
    gx_cached_set_tev_order_ex(stage, 0, 0, 0x30, 0, 0x40);
    gx_cached_set_tev_color_op(stage, (int)stageTex, (int)stage0, 0xff);
    gx_cached_set_tev_color_in(stage, 0xf, 0xe, 8, 0);
    gx_cached_set_tev_alpha_in(stage, 7, 7, 7, 0);
    gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 0, 1, 0);

    gx_cached_set_tev_color_op(stage1, 0xff, 0x100, 0xff);
    gx_cached_set_tev_color_in(stage1, 0xf, 6, 0, 0xf);
    gx_cached_set_tev_alpha_in(stage1, 7, 7, 7, 0);
    gx_cached_set_tev_color_env(stage1, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage1, 0, 0, 0, 1, 0);

    (void)outHi;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018dc68 | Name: projtex_konst_tev_stage | Date: 2026-09-06
// Program a single additive TEV stage for the projected texture using a grey
// konst colour (DAT_805acab0) at half-scale (0x3f prefix). Returns the next
// free TEV stage.
// ---------------------------------------------------------------------------
unsigned int projtex_konst_tev_stage(unsigned int stage)
{
    unsigned int konst = 0x3f000000u | ((unsigned int)DAT_805acab0);
    unsigned int sel = konst;
    FUN_803ca174(3, &sel);
    gx_cached_set_tev_color_op(stage, 0xff, 0xff, 0xff);
    gx_cached_set_tev_color_in(stage, 0, 6, 7, 0xf);
    gx_cached_set_tev_alpha_in(stage, 7, 7, 7, 0);
    gx_cached_set_tev_color_env(stage, 0, 0, 0, 1, 0);
    gx_cached_set_tev_alpha_env(stage, 0, 0, 0, 1, 0);
    return stage + 1;
}
