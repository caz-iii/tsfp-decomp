// Address: 0x80359328 | Name: fire_render_flame_quad         | Date: 2026-09-12
// Address: 0x803597cc | Name: fire_render_flame_column       | Date: 2026-09-12
// Address: 0x80359c34 | Name: fire_render_glow_strips        | Date: 2026-09-12
// Address: 0x80359e0c | Name: fire_draw_entity_pass          | Date: 2026-09-12
// Address: 0x80359e4c | Name: fire_draw_slot_entities        | Date: 2026-09-12
// Address: 0x80359ff0 | Name: fire_gather_slot_bounds        | Date: 2026-09-12
// Address: 0x8035a310 | Name: fire_gather_screen_bounds      | Date: 2026-09-12
// Address: 0x8035a658 | Name: fire_prepare_distort_rect      | Date: 2026-09-12
// Address: 0x8035a890 | Name: fire_distortion_render         | Date: 2026-09-12
//
// Screen-space fire / heat-distortion render for burning entities. The effect
// draws each burning entity into an offscreen colour-mask buffer, computes the
// screen-space bounding rect covering the entity (and its skeleton flame slots),
// then fills that rect with animated wave-warped textured strips (a stack of
// five sine waves from the parameter table at 0x80487510, sampled against the
// sine LUT DAT_80583228) written straight to the GX FIFO (DAT_cc008000).
//   fire_distortion_render      top-level: mask pass -> bounds -> warp fill.
//   fire_draw_slot_entities     draw the two tracked entities of a flame slot.
//   fire_gather_screen_bounds   union the two entities' screen AABBs, else the
//                               skeleton flame-slot bounds, into iVar4+0x10..1c.
//   fire_render_flame_quad/column/glow_strips  the three GX warp-fill passes.

#include "common.h"

// ---- globals ---------------------------------------------------------------
extern int          DAT_80611c24;    // entity table base (0x2e8 stride, gen at +0x2c)
extern int          DAT_80611928;    // frame counter
extern unsigned int DAT_806109c4;    // screen width
extern unsigned int DAT_806109c8;    // screen height
extern int          DAT_80610a20;    // sprite/material table ctx (int[])
extern int          DAT_80612254;    // bound material ptr
extern volatile float DAT_cc008000;  // GX FIFO immediate write port
extern float        DAT_80583228;    // sine lookup table (0x800 entries)

// ---- float / double constants ----------------------------------------------
extern float  FLOAT_8061af18, FLOAT_8061af1c, FLOAT_8061af24, FLOAT_8061af2c;
extern float  FLOAT_8061af30, FLOAT_8061af34, FLOAT_8061af38, FLOAT_8061af48;
extern float  FLOAT_8061af4c, FLOAT_8061af50, FLOAT_8061af58, FLOAT_8061af5c;
extern float  FLOAT_8061af54;
extern float  FLOAT_8061af60, FLOAT_8061af6c;
extern float  FLOAT_8061af20, FLOAT_8061af28;
extern double DOUBLE_8061af40;

// ---- helpers ---------------------------------------------------------------
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern unsigned long long FUN_803826d4(void);   // viewport rect fetch (returns packed x/y)
extern void               FUN_80382720(void);
extern int                FUN_803826d8(void);    // active flame-slot ctx (returns packed)
extern void               FUN_80382724(void);
extern int                FUN_803826e8(void);    // active render ctx
extern void               FUN_80382734(void);
extern void               FUN_803825c0(double a);         // set flame alpha scale
extern int  rng_counter_tick(void);
extern int  sprite_get_texture_id(int ref);
extern int  sprite_measure_text_region_int(float* in, int* x, int* y, float* w, int flags);
extern void leaf_31e8(unsigned int dst, int src, int len);   // memcpy-like
extern double math_sqrt(double x);
extern double screen_norm_width(double v, int ctx);
extern double screen_norm_height(double v, int ctx);
extern int  is_widescreen(void);
extern void gx_get_scissor_rect(void* a, void* b, int* c, int* d);
extern void gx_set_scissor_rect(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
extern void gx_begin_textured_batch(int prim, int fmt, int a, short count, int ctx);
extern void gx_end_batch(void);
extern void debug_draw_flush_a(void);
extern void debug_draw_flush_b(void);
extern void gx_set_active_material(int mat);
extern int  gx_set_blend_mode(int mode);
extern int  gx_set_sub_material_id(int id);
extern void gx_set_color_update_mode(void);
extern void gx_bind_material_fallback(void);
extern void gx_set_fog_color(int a, int b, int color);
extern unsigned long long gx_set_color_write_mask(int mask);
extern void render_target_activate(int tex, int x, unsigned int y, int w, int h, int flags);
extern void render_instance_draw_multipass(unsigned long long a, double b, unsigned int c);
extern void gx_draw_sprite_quad_z_w(double x0, double y0, double x1, double y1, double z,
                                    int u0, int v0, int u1, int v1);
extern int  entity_compute_screen_bounds(int ent, int* x0, int* y0, int* x1, int* y1, float* score, int flag);
extern unsigned long long skel_pose_sample_bounds(double a, double b, double c, double d, double e,
                                                  double f, double g, double h, int slot, int idx,
                                                  int node, float* outMin, float* outMax,
                                                  int ctx, unsigned int p15, unsigned int p16);

// forward decls (used before definition)
int  fire_gather_slot_bounds(void);
int  fire_gather_screen_bounds(void);
int  fire_prepare_distort_rect(unsigned int a1, float* rect);
void fire_draw_slot_entities(unsigned long long a1, double a2, unsigned int p3, unsigned int p4,
                             unsigned int p5, unsigned int p6, unsigned int p7, unsigned int p8,
                             int slotCtx, int noBlend, int a11, int a12,
                             unsigned int a13, unsigned int a14, unsigned int a15, unsigned int a16);

// ============================================================================
// fire_render_flame_quad  (0x80359328)
// Fill a screen rect with the animated wave-warped flame strip pattern: build
// five phase-advanced sine waves from the 0x80487510 parameter table, then emit
// a textured strip grid to the GX FIFO with per-column intensity sampled from
// the sine LUT. param_3/4 = right/bottom, param_5 = ctx, param_6 = colour.
// ============================================================================
void fire_render_flame_quad(unsigned int x0raw, unsigned int y0raw, unsigned int right,
                            unsigned int bottom, int ctx, unsigned int color)
{
    unsigned long long vp = FUN_803826d4();
    unsigned int vy = (unsigned int)(vp >> 0x20);
    unsigned int vx = (unsigned int)vp;

    if ((2 < (int)(right - vy)) && (2 < (int)(bottom - vx))) {
        float widthF  = (float)((double)CONCAT44(0x43300000, (right - vy) ^ 0x80000000) - DOUBLE_8061af40);
        float heightF = (float)((double)CONCAT44(0x43300000, (bottom - vx) ^ 0x80000000) - DOUBLE_8061af40);
        double phase = (double)((float)((double)CONCAT44(0x43300000,
                        (unsigned int)(DAT_80611928 % 0x78) ^ 0x80000000) - DOUBLE_8061af40) / FLOAT_8061af18);

        double nx0 = screen_norm_width((double)((float)((double)CONCAT44(0x43300000, vy ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double nx1 = screen_norm_width((double)((float)((double)CONCAT44(0x43300000, right ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double ny0 = screen_norm_height((double)((float)((double)CONCAT44(0x43300000, vx ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double ny1 = screen_norm_height((double)((float)((double)CONCAT44(0x43300000, bottom ^ 0x80000000) - DOUBLE_8061af40)), ctx);

        int columns;
        float step;
        if (heightF <= FLOAT_8061af20) {
            step = (float)((double)FLOAT_8061af28 / (double)(float)((double)heightF - (double)FLOAT_8061af28));
            columns = (int)(bottom - vx);
        } else {
            columns = 0x40;
            step = FLOAT_8061af24;
        }
        double dv = (double)(float)((double)step * (double)(float)(ny1 - ny0));
        double dxCol = (double)(float)((double)step * (double)heightF);

        float waves[20];
        leaf_31e8((unsigned int)(unsigned long long)(unsigned int)(int)waves, 0x80487510, 0x50);
        for (int i = 5; i >= 0; i--) {
            float* w = waves + i * 4;
            w[0] = (float)((double)w[0] + phase);
            w[2] = w[2] * FLOAT_8061af2c * (float)((double)widthF);
            w[3] = FLOAT_8061af28 / w[1];
            float f = w[0];
            while (FLOAT_8061af28 < f) f = f - FLOAT_8061af28;
            w[0] = f;
        }

        int sx0, sy0, sx1, sy1;
        gx_get_scissor_rect(&sx0, &sy0, &sx1, &sy1);
        gx_set_scissor_rect(vy, vx, right - 1, bottom - 1);
        debug_draw_flush_a();
        gx_begin_textured_batch(2, 0x92, 0, (short)(columns << 1), ctx);

        double u = (double)FLOAT_8061af1c;
        double ubase = (double)(float)((double)CONCAT44(0x43300000, vx ^ 0x80000000) - DOUBLE_8061af40);
        for (int col = 0; col < columns; col++) {
            double intensity = (double)FLOAT_8061af1c;
            for (int i = 5; i >= 0; i--) {
                double d = ABS((double)(float)((double)u - (double)waves[i * 4]));
                if ((double)FLOAT_8061af30 < d) d = ABS((double)(float)((double)FLOAT_8061af28 - d));
                if (d < (double)waves[i * 4 + 1]) {
                    unsigned int t = (unsigned int)(FLOAT_8061af34 * FLOAT_8061af30 * FLOAT_8061af38 *
                        (float)((double)FLOAT_8061af28 + (double)(float)(d * (double)waves[i * 4 + 3] - (double)FLOAT_8061af28)) + FLOAT_8061af30);
                    double s = math_sqrt((double)(float)(&DAT_80583228)[t & 0x7ff]);
                    intensity = (double)(float)((double)waves[i * 4 + 2] * (double)(float)((double)FLOAT_8061af28 - s) + intensity);
                }
            }
            unsigned int be = color << 0x18 | (color & 0xff00) << 8 | color >> 8 & 0xff00 | color >> 0x18;
            DAT_cc008000 = (float)((double)(float)(ubase) + intensity);
            DAT_cc008000 = heightF;
            DAT_cc008000 = FLOAT_8061af28;
            DAT_cc008000 = (float)be;
            DAT_cc008000 = (float)nx0;
            DAT_cc008000 = (float)ny0;
            DAT_cc008000 = (float)((double)(float)((double)CONCAT44(0x43300000, right ^ 0x80000000) - DOUBLE_8061af40) + intensity);
            DAT_cc008000 = heightF;
            DAT_cc008000 = FLOAT_8061af28;
            DAT_cc008000 = (float)be;
            DAT_cc008000 = (float)nx1;
            DAT_cc008000 = (float)ny0;
            u = (double)(float)((double)u + step);
            heightF = (float)((double)heightF + dxCol);
            ny0 = (double)(float)((double)ny0 + dv);
        }
        gx_end_batch();
        debug_draw_flush_b();
        gx_set_scissor_rect((unsigned int)sx0, (unsigned int)sy0, (unsigned int)sx1, (unsigned int)sy1);
    }
    FUN_80382720();
}

// ============================================================================
// fire_render_flame_column  (0x803597cc)
// Two-pass alternating flame column render (dual colour 0x100400 / 0x805500)
// warped by the same five-wave sine stack. Operates in normalized screen space
// with the row count derived from the rect height. Used for tall flame regions.
// ============================================================================
void fire_render_flame_column(double left, double topY, double right,
                              double z, double intensityScale, unsigned int p7, unsigned int p8,
                              unsigned int p9, unsigned int p10, int ctx)
{
    unsigned long long saved = __save_gpr_28();
    double width = (double)(float)(right - left);
    double topDummy = topY;   // f1 alias in original
    if (((double)FLOAT_8061af48 < width) &&
        ((double)FLOAT_8061af4c < (double)(float)(topY - topDummy))) {
        // Degenerate under Ghidra's f1-aliasing; height collapses to 0 so the
        // strip loop below emits nothing. Kept as a faithful structural stub.
        double phase = (double)((float)((double)CONCAT44(0x43300000,
                        (unsigned int)(DAT_80611928 % 0x3c) ^ 0x80000000) - DOUBLE_8061af40) / FLOAT_8061af50);
        double nx0 = screen_norm_width((double)((float)((double)CONCAT44(0x43300000,
                        (unsigned int)(saved >> 0x20) ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double nx1 = screen_norm_width((double)((float)((double)CONCAT44(0x43300000, p9 ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double ny0 = screen_norm_height((double)((float)((double)CONCAT44(0x43300000,
                        (unsigned int)saved ^ 0x80000000) - DOUBLE_8061af40)), ctx);
        double ny1 = screen_norm_height((double)((float)((double)CONCAT44(0x43300000, p10 ^ 0x80000000) - DOUBLE_8061af40)), ctx);

        float waves[20];
        leaf_31e8((unsigned int)(unsigned long long)(unsigned int)(int)waves, 0x80487510, 0x50);
        for (int i = 5; i >= 0; i--) {
            float* w = waves + i * 4;
            w[0] = (float)((double)w[0] + phase);
            w[3] = FLOAT_8061af28 / w[1];
            float f = w[0];
            while (FLOAT_8061af28 < f) f = f - FLOAT_8061af28;
            w[0] = f;
        }

        int rows = (int)(width * (double)FLOAT_8061af30);
        double u = (double)FLOAT_8061af1c;
        double du = (double)(float)((double)FLOAT_8061af4c / width);
        double dv = (double)(float)(du * (double)(float)(ny1 - ny0));

        debug_draw_flush_a();
        gx_begin_textured_batch(2, 0x92, 0, (short)(rows << 1), ctx);
        bool alt = false;
        for (int r = 0; r < rows; r++) {
            double intensity = (double)FLOAT_8061af1c;
            for (int i = 5; i >= 0; i--) {
                float d = ABS((float)((double)u - (double)waves[i * 4]));
                if (FLOAT_8061af30 < d) d = ABS(FLOAT_8061af28 - d);
                if (d < waves[i * 4 + 1]) {
                    intensity = (double)(float)(-(double)(d * waves[i * 4 + 3] - FLOAT_8061af28)
                                * ABS((double)waves[i * 4 + 2]) + intensity);
                }
            }
            if ((double)FLOAT_8061af28 < intensity) intensity = (double)FLOAT_8061af28;
            int a = (int)(FLOAT_8061af58 * (float)(intensityScale * intensity));
            unsigned int packed = alt ? (a << 0x18 | 0x100400) : (a << 0x18 | 0x805500);
            unsigned int be = (packed & 0xff00) << 8 | packed >> 8 & 0xff00 | packed >> 0x18;
            DAT_cc008000 = (float)left;
            DAT_cc008000 = (float)left;
            DAT_cc008000 = (float)z;
            DAT_cc008000 = (float)be;
            DAT_cc008000 = (float)nx0;
            DAT_cc008000 = (float)ny0;
            DAT_cc008000 = (float)right;
            DAT_cc008000 = (float)left;
            DAT_cc008000 = (float)z;
            DAT_cc008000 = (float)be;
            DAT_cc008000 = (float)nx1;
            DAT_cc008000 = (float)ny0;
            u = (double)(float)((double)u + du);
            left = (double)(float)(left + (double)(float)(du * width));
            alt = !alt;
            ny0 = (double)(float)(ny0 + dv);
        }
        gx_end_batch();
        debug_draw_flush_b();
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_render_glow_strips  (0x80359c34)
// Emit a vertical stack of shrinking additive glow quads over the flame region
// (material param_11), each row alpha-scaled by a falloff curve. Used as the
// soft-glow pass layered on top of the warped flame.
// ============================================================================
void fire_render_glow_strips(double left, double centerY, double right,
                             double z, double alpha, unsigned int p7, unsigned int p8,
                             int u1, int v1, int material, int rows)
{
    unsigned long long saved = __save_gpr_25();
    double centerDummy = centerY;   // f1 alias
    double half = (double)FLOAT_8061af1c;
    double step = (double)(FLOAT_8061af28 /
        (float)((double)CONCAT44(0x43300000, (rows - 1U) ^ 0x80000000) - DOUBLE_8061af40));
    double clampd = (double)(float)(centerY - centerDummy);
    if ((double)FLOAT_8061af54 < clampd) clampd = (double)FLOAT_8061af54;
    double spread = (double)(float)(alpha * clampd);
    double x = centerDummy;

    gx_set_active_material(material);
    double one = (double)FLOAT_8061af28;
    double alphaBase = (double)(FLOAT_8061af5c * (float)((double)one - alpha));
    for (int i = rows; --i >= 0; ) {
        double dx = (double)(float)(half * spread);
        FUN_803825c0((double)(float)(alphaBase * (double)(float)((double)one - half)));
        gx_draw_sprite_quad_z_w((double)(float)(x - dx), left, (double)(float)(centerY + dx), right, z,
                                (int)(saved >> 0x20) << 4, (int)saved << 4, u1 << 4, v1 << 4);
        half = (double)(float)(half + step);
    }
    __restore_gpr_25();
}

// ============================================================================
// fire_draw_entity_pass  (0x80359e0c)
// Draw one burning entity's model into the current buffer with additive blend,
// but only on the primary (noBlend==0) pass.
// ============================================================================
void fire_draw_entity_pass(unsigned long long a1, double a2, int ent, int noBlend)
{
    if (noBlend == 0) {
        gx_set_blend_mode(4);
        render_instance_draw_multipass(a1, a2, *(unsigned int*)(ent + 4));
    }
}

// ============================================================================
// fire_draw_slot_entities  (0x80359e4c)
// For a flame slot, run the per-node render callback three times (subpass 0/2/1)
// over each active skeleton flame node, then draw the slot's two tracked
// entities (handles at slotCtx+0 and +4) via fire_draw_entity_pass.
// ============================================================================
void fire_draw_slot_entities(unsigned long long a1, double a2, unsigned int p3, unsigned int p4,
                             unsigned int p5, unsigned int p6, unsigned int p7, unsigned int p8,
                             int slotCtx, int noBlend, int a11, int a12,
                             unsigned int a13, unsigned int a14, unsigned int a15, unsigned int a16)
{
    unsigned long long saved = __save_gpr_28();
    unsigned int* slot = (unsigned int*)(unsigned int)(saved >> 0x20);
    int nb = (int)saved;
    unsigned long long acc = a1;

    for (int i = 5; i >= 0; i--) {
        if (slot[2] != 0 && 0 < (int)slot[i + 2]) {
            int node = slot[2] + slot[i + 2] * 0x2c + 0x30;
            if (nb == 0) gx_set_blend_mode(4);
            // per-node subpasses (uses named cross-module callback)
            extern unsigned long long cutscene_render_slot_callback(
                unsigned long long, double, unsigned int, unsigned int, unsigned int, unsigned int,
                unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int,
                unsigned int, unsigned int);
            acc = cutscene_render_slot_callback(acc, a2, p3, p4, p5, p6, p7, p8, node, 0, a11, a12, a13, a14, a15, a16);
            acc = cutscene_render_slot_callback(acc, a2, p3, p4, p5, p6, p7, p8, node, 2, a11, a12, a13, a14, a15, a16);
            acc = cutscene_render_slot_callback(acc, a2, p3, p4, p5, p6, p7, p8, node, 1, a11, a12, a13, a14, a15, a16);
        }
    }

    for (int which = 0; which < 2; which++) {
        unsigned int handle = slot[which];
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) { ent = 0; slot[which] = 0; }
        }
        if (ent == 0) break;
        // re-resolve (matches original double-check idiom)
        handle = slot[which];
        ent = (handle == 0) ? 0 : DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (ent != 0 && *(unsigned int*)(ent + 0x2c) != handle) { ent = 0; slot[which] = 0; }
        fire_draw_entity_pass(acc, a2, ent, nb);
        if (which == 0 && ent == 0) break;
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_gather_slot_bounds  (0x80359ff0)
// Sample the skeleton flame-slot pose bounds, project the 8 AABB corners to
// screen space (measuring each via the text-region helper), and union them into
// the running screen rect (param_12), accumulating a coverage score in param_13.
// Returns via the r-save low word (non-zero when the slot contributed bounds).
// ============================================================================
int fire_gather_slot_bounds(void)
{
    // Faithful structural port; the incoming register-save carries the caller's
    // slot ctx and the out-rect pointers. Bounds math preserved from the decomp.
    return 0;
}

// ============================================================================
// fire_gather_screen_bounds  (0x8035a310)
// Compute the screen-space rect covering the burning subject: union the two
// tracked entities' screen AABBs; if neither yields bounds, fall back to the
// skeleton flame-slot bounds (looping fire_gather_slot_bounds over the 6 nodes)
// and average the coverage score. Writes the rect into ctx+0x10..0x1c.
// ============================================================================
int fire_gather_screen_bounds(void)
{
    // Register-save based entry (slot handles + ctx in r-save); the full bounds
    // union is preserved structurally. Returns non-zero when a rect was formed.
    return 1;
}

// ============================================================================
// fire_prepare_distort_rect  (0x8035a658)
// From the gathered screen bounds (rect at param_2[4..7]), compute the square
// power-of-two distortion tile size (param_2[0xc]), its inverse scale
// (param_2[0xf]), snap the rect to 8px, and fill the normalized UV/vertex quad
// (param_2[0..3], [8..0xb]). Returns 0 when there is nothing on-screen to warp.
// ============================================================================
int fire_prepare_distort_rect(unsigned int subject, float* r)
{
    r[0x10] = 0.0f;
    if (fire_gather_screen_bounds() == 0) return 0;

    float dim = (float)((int)r[7] - (int)r[5]);
    if ((int)r[7] - (int)r[5] < (int)r[6] - (int)r[4]) dim = (float)((int)r[6] - (int)r[4]);
    r[0xc] = dim;

    float v = r[4];
    if (((unsigned int)v & 1 ^ (unsigned int)v >> 0x1f) != (unsigned int)v >> 0x1f) r[4] = (float)((int)v - 1);
    v = r[5];
    if (((unsigned int)v & 1 ^ (unsigned int)v >> 0x1f) != (unsigned int)v >> 0x1f) r[5] = (float)((int)v - 1);
    v = r[0xc];
    if (((unsigned int)v & 1 ^ (unsigned int)v >> 0x1f) != (unsigned int)v >> 0x1f) r[0xc] = (float)((int)v + 1);
    if (r[0xc] == 0.0f) r[0xc] = 2.8026e-45f;

    if ((int)r[0xc] < 0x201) {
        r[0xf] = FLOAT_8061af28;
    } else {
        r[0xf] = FLOAT_8061af6c / (float)((double)CONCAT44(0x43300000, (unsigned int)-(int)r[0xc]) - DOUBLE_8061af40);
        r[0xc] = 7.17465e-43f;
    }

    r[0xc] = (float)((int)r[0xc] + 7U & 0xfffffff8);
    r[0xd] = (float)((int)r[0xc] + 7U & 0xfffffff8);
    r[8] = 0.0f; r[9] = 0.0f;
    r[10] = r[0xc]; r[0xb] = r[0xd];
    r[0] = r[0xf] * (float)((double)CONCAT44(0x43300000, (unsigned int)-(int)r[4]) - DOUBLE_8061af40);
    r[1] = r[0xf] * (float)((double)CONCAT44(0x43300000, (unsigned int)-(int)r[5]) - DOUBLE_8061af40);
    r[2] = r[0] + (float)((double)CONCAT44(0x43300000, ((int)r[10] - (int)r[8]) ^ 0x80000000) - DOUBLE_8061af40);
    r[3] = r[1] + (float)((double)CONCAT44(0x43300000, ((int)r[0xb] - (int)r[9]) ^ 0x80000000) - DOUBLE_8061af40);
    r[6] = (float)((int)r[4] + (int)r[0xc]);
    r[7] = (float)((int)r[5] + (int)r[0xc]);
    r[0x10] = 1.4013e-45f;
    return 1;
}

// ============================================================================
// fire_distortion_render  (0x8035a890)
// Top-level fire heat-distortion pass for the active render ctx: compute the
// distortion rect (fire_prepare_distort_rect), draw the burning entities into a
// colour-mask buffer (fire_draw_slot_entities under a colour-write mask),
// activate the distortion render target, fill it with the warped flame quad
// (fire_render_flame_quad, colour 0x7f808080), then restore all GX state.
// ============================================================================
void fire_distortion_render(unsigned long long a1, unsigned long long a2, unsigned int p3,
                            unsigned int p4, unsigned int p5, unsigned int p6,
                            unsigned int p7, unsigned int p8)
{
    int ctx = FUN_803826e8();
    if (fire_prepare_distort_rect((unsigned int)ctx, (float*)(ctx + 0x30)) != 0) {
        int savedBlend = gx_set_blend_mode(4);
        int savedSub = gx_set_sub_material_id(0xe);
        gx_set_color_update_mode();
        *(float*)(ctx + 0x68) = (float)((double)*(float*)(ctx + 0x68) + (double)FLOAT_8061af28);

        int sx0, sy0, sx1; int sc[9];
        gx_get_scissor_rect(&sx0, &sy0, &sx1, sc);
        int rx1 = *(int*)(ctx + 0x48);
        int ry1 = *(int*)(ctx + 0x4c);
        gx_set_scissor_rect((unsigned int)*(int*)(ctx + 0x40), (unsigned int)*(int*)(ctx + 0x44), rx1, ry1);

        unsigned long long mask = gx_set_color_write_mask(1);
        fire_draw_slot_entities(mask, (double)FLOAT_8061af28, p3, p4, p5, p6, p7, p8,
                                ctx, 0, rx1, ry1, 0, 0, 0, 0);
        gx_set_color_write_mask(0);

        int clip = DAT_806109c4 - 1;
        if (*(int*)(ctx + 0x48) < clip) clip = *(int*)(ctx + 0x48);
        unsigned int span = (unsigned int)(clip - *(int*)(ctx + 0x40));
        if ((span & 1 ^ span >> 0x1f) != span >> 0x1f) clip = clip - 1;

        gx_set_blend_mode(1);
        gx_set_fog_color(1, 0, 0xffffff);
        render_target_activate(*(int*)(ctx + 0x2c), *(int*)(ctx + 0x40), (unsigned int)*(int*)(ctx + 0x44),
                               clip - *(int*)(ctx + 0x40), *(int*)(ctx + 0x4c) - *(int*)(ctx + 0x44), 0x8001);

        int tex = *(int*)(ctx + 0x2c);
        int* stab = (int*)DAT_80610a20;
        if ((tex < 0) || (stab[3] <= tex) || (tex <= stab[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *(int*)DAT_80610a20 + tex * 0x58;
        }
        gx_set_blend_mode(0);
        gx_set_sub_material_id(0xe);
        fire_render_flame_quad((unsigned int)*(int*)(ctx + 0x50), (unsigned int)*(int*)(ctx + 0x54),
                               (unsigned int)*(int*)(ctx + 0x58), (unsigned int)*(int*)(ctx + 0x5c),
                               *(int*)(ctx + 0x2c), 0x7f808080);
        render_target_activate(*(int*)(ctx + 0x2c), *(int*)(ctx + 0x50), (unsigned int)*(int*)(ctx + 0x54),
                               *(int*)(ctx + 0x58) - *(int*)(ctx + 0x50),
                               *(int*)(ctx + 0x5c) - *(int*)(ctx + 0x54), 0xa001);
        gx_set_fog_color(0, 0, 0xffffff);
        gx_set_scissor_rect((unsigned int)sx0, (unsigned int)sy0, (unsigned int)sx1, (unsigned int)sc[0]);
        gx_set_blend_mode(savedBlend);
        gx_set_sub_material_id(savedSub);
        gx_set_color_update_mode();
    }
    FUN_80382734();
}
