// Batch: batch_967 | Module: arcade/challenge editor preview panel draw
// Address range: 0x8033f1e8 - 0x8033fe28
// Date: 2026-09-12
//
// The arcade/challenge editor's preview-panel render pass (continues batches 964-966). Sets up the
// preview viewport/scissor rect, draws the queued brush segments (two passes: fully-placed and
// fading), draws the single-tile placement cursor with its animated highlight and spawn particle,
// draws the tooltip/info text boxes for the hovered cell/pad, and finally the live preview entities.
// The preview/cursor context is DAT_80612d68 (word-indexed working buffer); the cursor entity is
// DAT_80612be4+0x4150/0x4154; the per-cell tile handles are DAT_80612be4+0x40dc/0x4068.
// Callers: aveditor_render_screen.
//
//   aveditor_preview_render_particles 0x8033f1e8  render the placement particle if the cursor is a
//                                                 particle entity (kind 0x798).
//   aveditor_preview_draw             0x8033f224  the full preview-panel draw pass.
//   aveditor_cursor_calc_world_pos    0x8033fe28  compute the cursor world position from depth/rot.
//
// NON-MATCHING: aveditor_preview_draw reproduces the decompiler's structure; the pervasive
// int->double magic-constant float conversions and the "round to int" (>>1 + sign-adjust) halving
// are reproduced faithfully. The particle-spawn / rng branch that seeds the placement spark is kept
// structurally. The many render helpers are declared file-locally with call-site-accurate types
// (their generated_decls signatures use different paired-single conventions; this TU includes only
// common.h). DAT_80612d68 is treated as int* (word index) to mirror DAT_80612d68[N] word accesses.
// The three inline blocks of aveditor_preview_draw (brush-segment draw, placement-spark spawn, info
// tooltips) are extracted into file-local `static` helpers (draw_brush_segment / draw_placement_spark
// / draw_info_tooltips) purely for readability; they are NOT separate binary functions.

#include "common.h"

extern "C" {

extern double math_sqrt(double x);   // named sqrt helper (as used across the tree)

extern int    DAT_80612be4;   // editor context base
extern int*   DAT_80612d68;   // cursor/preview context (word-indexed)
extern int    DAT_806119ec;   // active root-screen index
extern int    DAT_806109c0;   // preview aspect source (int, magic-const)

// placed-cell record block
extern int    DAT_80477c80;   // cell flags (int; 0xc4 byte == 0x31 int stride)
extern int    DAT_80477bc8;   // cell name string-id (short)
extern int    DAT_80477bcc;   // cell packed colour

// pad descriptor table (0x140 stride)
extern char   DAT_8047c14c;   // pad icon byte
extern int    DAT_8047c150;   // pad name string-id

extern int    DAT_8051a8f8;   // camera field (per root screen, 0x1e0 stride)

// brush anchor / camera globals (shared with the camera module); contiguous block from c88
extern float  DAT_80561c88;   // cursor world X (+0)
extern float  DAT_80561c8c;   // cursor world Y (+4)
extern float  DAT_80561c90;   // cursor world Z (+8)
extern float  DAT_80561c94;   // camera eye delta X (+0xc)
extern float  DAT_80561c98;   // camera eye delta Y (+0x10)
extern float  DAT_80561c9c;   // camera eye delta Z (+0x14)

// preview spawn transform / particle-colour blocks
extern float  DAT_803e0acc;   // cursor highlight dir X
extern float  DAT_803e0ad0;   // cursor highlight dir Y
extern float  DAT_803e0ad4;   // cursor highlight dir Z
extern float  DAT_803e0ad8;   // single-cursor highlight dir X
extern float  DAT_803e0adc;   // single-cursor highlight dir Y
extern float  DAT_803e0ae0;   // single-cursor highlight dir Z
extern int    DAT_803e0ae4;   // particle spawn block word
extern int    DAT_803e0ae8;
extern int    DAT_803e0aec;

extern float  FLOAT_8060fad0; // depth-range bias
extern float  FLOAT_806108ec; // spark timer scale
extern float  FLOAT_8061ab2c; // half / normalize const
extern float  FLOAT_8061ab34; // 0.0f
extern float  FLOAT_8061ab50; // spark spread scale
extern float  FLOAT_8061ab58; // 1.0f
extern float  FLOAT_8061ab6c; // spark min-life
extern float  FLOAT_8061ab84; // 1.0f / squash
extern float  FLOAT_8061ab94; // depth-range base
extern float  FLOAT_8061ab98; // depth-range far
extern float  FLOAT_8061ab9c; // length-epsilon
extern float  FLOAT_8061aba0; // spark timer base
extern float  FLOAT_8061aba4; // spark timer scale
extern float  FLOAT_8061aba8; // rng normalize divisor
extern float  FLOAT_8061abac; // spark base pos X
extern float  FLOAT_8061abb0; // spark base pos Y
extern float  FLOAT_8061abb4; // spark base pos Z
extern float  FLOAT_8061abb8; // spark spin
extern float  FLOAT_8061abc0; // spark opacity / tooltip X scale
extern float  FLOAT_8061abc4; // tooltip Y scale
extern float  FLOAT_80611a64; // tooltip layout const
extern float  FLOAT_80611a68; // tooltip layout const
extern float  FLOAT_80611a6c; // tooltip layout const
extern float  FLOAT_80611a70; // tooltip layout const
extern float  FLOAT_8061ab30; // grid spacing Y
extern double DOUBLE_8061ab48;// int->double bias (unsigned)
extern double DOUBLE_8061ab70;// int->double bias (signed)

// named cross-module helpers (call-site-accurate types; this TU includes only common.h)
extern void   particle_system_render_dispatch(void);
extern int    gx_set_blend_mode(int index);
extern void   viewport_save_active(void);
extern void   viewport_set_rect_active(int left, int top, int right, int bottom);
extern void   sprite_draw_bordered_frame(int left, int top, int right, int bottom, int blend);
extern void   gx_set_scissor_rect(unsigned left, unsigned top, int right, int bottom);
extern void   viewport_set_depth_params_active(double a, double b, double c);
extern double viewport_set_gx_active(void);
extern unsigned gx_set_sub_material_id(int id);
extern void   viewport_swap_active_saved(void);
extern void   mtx4_copy(float* src, float* dst);
extern unsigned aveditor_tick_pulse(void);
extern double render_entity_submit_multipass_indexed(unsigned long long ctx, double param);
extern double render_entity_submit_multipass(unsigned long long ctx, double param);
extern unsigned long long render_instance_draw_multipass(double ctx, double param, int instance);
extern double particle_slot_set_opacity(double opacity, int* slot);
extern unsigned rng_counter_tick(void);
extern int*   particle_spawn_directed_full(int** slotOut, int type, int flags, float* pos,
                                           float* vel, int* colorBlock, unsigned* extra);
extern unsigned aveditor_count_placed_for_type(int type);
extern int    aveditor_layout_index_of_type(int type);
extern int    aveditor_get_palette_entry_label(int type);
extern int    get_string_by_id(int id);
extern void   text_set_font_table(int table);
extern double text_draw_char_shadowed(double x, double y, unsigned text, unsigned fg, unsigned bg);
extern int    aveditor_draw_tooltip_box(double f1, double f2, double f3, long long f4, double f5,
                                        double f6, double f7, unsigned long long f8, unsigned x,
                                        int y, int icon, char* text, unsigned wrap, int underline,
                                        int r9, int* r10);
extern unsigned color_expand_7bit(unsigned packed);
extern unsigned debug_log_formatted(unsigned long long f1, double f2, double f3, long long f4,
                                    double f5, double f6, double f7, unsigned long long f8, int fmt,
                                    unsigned a10, int a11, char* a12, unsigned a13, unsigned a14,
                                    int r9, int* r10);
extern int    aveditor_get_pad_tile_count(int padIndex);
extern void   aveditor_preview_render(unsigned long long r3, double r4);
extern void   mtx4_set_identity(int mtx);
extern void   mtx4_set_rotation_zyx(double rz, double ry, double rx, float* mtx);
extern void   mtx4_transform_vector(float* mtx, float* vec);

// _savegpr_/_restgpr_ runtime helpers
extern void   FUN_803826dc(void);
extern void   FUN_80382728(void);

// forward decl within this TU
void aveditor_preview_render_particles(void);

extern float  FLOAT_8061ab78; // rotation numerator
extern float  FLOAT_8061ab3c; // rotation denominator

} // extern "C"

// -----------------------------------------------------------------------------

// Render the placement particle: when the cursor entity (DAT_80612be4+0x4150) is a particle entity
// (kind 0x798), dispatch the particle system render.
void aveditor_preview_render_particles(void)
{
    if (*(int*)(DAT_80612be4 + 0x4150) != 0 &&
        *(short*)(*(int*)(DAT_80612be4 + 0x4150) + 0x94) == 0x798) {
        particle_system_render_dispatch();
    }
}

// Round-half helper matching the decompiler's ((x>>1) + (x<0 && (x&1))) idiom.
static inline int half_round(int x)
{
    return (x >> 1) + (unsigned)(x < 0 && (x & 1) != 0);
}

// Compute the cursor world position (DAT_80561c88..90) from the cursor depth (+0x38) and rotation
// (+0x3c/+0x40): build a ZYX rotation, transform the depth vector, then add the drag-rect origin.
void aveditor_cursor_calc_world_pos(void)
{
    float m[17];
    DAT_80561c88 = *(float*)((char*)DAT_80612d68 + 0x38);
    DAT_80561c8c = FLOAT_8061ab34;
    DAT_80561c90 = FLOAT_8061ab34;
    mtx4_set_identity((int)(intptr_t)m);
    mtx4_set_rotation_zyx((double)FLOAT_8061ab34,
        (double)((FLOAT_8061ab78 * *(float*)((char*)DAT_80612d68 + 0x3c)) / FLOAT_8061ab3c),
        (double)((FLOAT_8061ab78 * *(float*)((char*)DAT_80612d68 + 0x40)) / FLOAT_8061ab3c), m);
    mtx4_transform_vector(m, &DAT_80561c88);
    DAT_80561c88 = DAT_80561c88 + *(float*)((char*)DAT_80612d68 + 0x44);
    DAT_80561c8c = DAT_80561c8c + *(float*)((char*)DAT_80612d68 + 0x48);
    DAT_80561c94 = *(float*)((char*)DAT_80612d68 + 0x44);
    DAT_80561c98 = *(float*)((char*)DAT_80612d68 + 0x48);
    DAT_80561c90 = DAT_80561c90 + *(float*)((char*)DAT_80612d68 + 0x4c);
    DAT_80561c9c = *(float*)((char*)DAT_80612d68 + 0x4c);
}

// Extracted sub-blocks of aveditor_preview_draw (static, internal to this one binary function; not
// separate binary functions). They mirror the decompiler's inline blocks to keep the draw readable.

// Draw one queued brush segment (record at DAT_80612d68 + off): copy its transform into the pad
// tile entity, set the animated highlight colour, submit the multipass draw, then restore the tile.
static double draw_brush_segment(int off, double ctx)
{
    int t = aveditor_get_pad_tile_count(*(int*)(*(int*)((int)DAT_80612d68 + off + 0x5d8) + 0x10));
    int te = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
    *(int*)(te + 0x64) = *(int*)((int)DAT_80612d68 + off + 0x5a8);
    *(int*)(te + 0x68) = *(int*)((int)DAT_80612d68 + off + 0x5ac);
    *(int*)(te + 0x6c) = *(int*)((int)DAT_80612d68 + off + 0x5b0);
    mtx4_copy((float*)((int)DAT_80612d68 + off + 0x5dc), *(float**)(*(int*)(te + 4) + 0x10));
    unsigned savedMat = *(unsigned*)(*(int*)(te + 4) + 0x30);
    unsigned short savedFlag = *(unsigned short*)(te + 0x14);
    *(unsigned*)(*(int*)(te + 4) + 0x30) = *(unsigned*)((int)DAT_80612d68 + off + 0x5d4);
    *(unsigned*)(te + 0x28) |= 0x200008;
    color_expand_7bit(aveditor_tick_pulse());
    ctx = render_entity_submit_multipass_indexed((unsigned long long)ctx, ctx);
    *(float*)(te + 0x64) = FLOAT_8061ab34;
    *(float*)(te + 0x68) = FLOAT_8061ab34;
    *(float*)(te + 0x6c) = FLOAT_8061ab34;
    *(unsigned*)(*(int*)(te + 4) + 0x30) = savedMat;
    *(unsigned short*)(te + 0x14) = savedFlag;
    return ctx;
}

// Seed / advance the placement "spark" particle attached to the cursor entity. Structural
// reproduction of the rng-driven spawn path (FLOAT/rng constants preserved).
static void draw_placement_spark(int cursorEnt)
{
    int* slot = *(int**)(cursorEnt + 0x7c);
    double aspect = (double)FLOAT_8061ab84 /
        (double)(float)((double)(CONCAT44(0x43300000, (unsigned)DAT_806109c0) ^ 0x80000000) - DOUBLE_8061ab70);
    float timer = -(float)((double)FLOAT_806108ec * aspect -
                           (double)*(float*)(*(int*)(DAT_80612be4 + 0x4150) + 0x14c));
    if (timer < FLOAT_8061ab34) {
        unsigned r = rng_counter_tick();
        float t = (float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061ab48);
        timer = FLOAT_8061aba4 * (t / FLOAT_8061aba8) + FLOAT_8061aba0;
        if (slot != (int*)0) {
            particle_slot_set_opacity((double)FLOAT_8061ab34, slot);
        }
    }
    if (slot != (int*)0 && (double)(float)slot[0xc] < (double)FLOAT_8061ab6c) {
        particle_slot_set_opacity((double)FLOAT_8061ab34, slot);
        slot[5] = 0;
        slot = (int*)0;
    }
    if (slot == (int*)0) {
        float pos[3] = { FLOAT_8061abac, FLOAT_8061abb0, FLOAT_8061abb4 };
        float vel[3];
        vel[0] = FLOAT_8061ab50 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_8061ab48) / FLOAT_8061aba8 - FLOAT_8061ab2c);
        vel[1] = FLOAT_8061ab50 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_8061ab48) / FLOAT_8061aba8 - FLOAT_8061ab2c);
        vel[2] = FLOAT_8061ab50 * ((float)((double)CONCAT44(0x43300000, rng_counter_tick()) - DOUBLE_8061ab48) / FLOAT_8061aba8 - FLOAT_8061ab2c);
        int spawnBlock[3] = { DAT_803e0ae4, DAT_803e0ae8, DAT_803e0aec };
        DAT_80612d68[0x10] = (int)FLOAT_8061abb8;
        slot = particle_spawn_directed_full(&slot, 0x5f, 0x20000000, pos, vel, spawnBlock, (unsigned*)0);
        particle_slot_set_opacity((double)FLOAT_8061abc0, slot);
        unsigned col = *(unsigned*)(&DAT_80477bcc + DAT_80612d68[0x22] * 0xc4);
        *(unsigned*)(slot[6] + 0xfa4) =
            col << 0x18 | (col & 0xff00) << 8 | (col >> 8) & 0xff00 | col >> 0x18;
    }
    if (slot != (int*)0) {
        *(int**)(*(int*)(DAT_80612be4 + 0x4150) + 0x7c) = slot;
    }
    *(float*)(*(int*)(DAT_80612be4 + 0x4150) + 0x14c) = timer;
}

// Draw the hovered cell/pad info tooltips (name + optional label + placed-count) at the layout
// position, using the tooltip-box helper and font tables. Structural reproduction.
static void draw_info_tooltips(double ctx)
{
    int panelState = *(int*)(DAT_80612be4 + 0x200);
    if (DAT_80612d68[0x24] < 0) {
        if (-1 < DAT_80612d68[0x22]) {
            unsigned count = aveditor_count_placed_for_type(DAT_80612d68[0x22]);
            int cell = DAT_80612d68[0x22];
            char* name = (char*)get_string_by_id((int)*(short*)(&DAT_80477bc8 + cell * 0xc4));
            char* label = (char*)0;
            int layoutIdx = aveditor_layout_index_of_type(DAT_80612d68[0x22]);
            if (((&DAT_80477c80)[cell * 0x31] & 0x1000) != 0) {
                label = (char*)aveditor_get_palette_entry_label(DAT_80612d68[0x22]);
            }
            text_set_font_table(4);
            int px = (int)((double)FLOAT_80611a68 *
                (double)(float)((double)FLOAT_8061ab30 + (double)*(float*)(&DAT_8051a8f8 + DAT_806119ec * 0x1e0)) +
                (double)FLOAT_80611a70);
            int py = (int)((double)FLOAT_8061abc0 * (double)FLOAT_80611a64 +
                (double)(float)((double)FLOAT_8061ab30 * (double)FLOAT_80611a64 + (double)FLOAT_80611a6c));
            unsigned wrap = (unsigned)((double)(float)((double)FLOAT_8061abc4 * (double)FLOAT_80611a68 + (double)FLOAT_80611a70) -
                (double)(float)((double)(CONCAT44(0x43300000, (unsigned)px) ^ 0x80000000) - DOUBLE_8061ab70));
            int icon = (panelState != 0) ? layoutIdx : -1;
            int adv = aveditor_draw_tooltip_box((double)FLOAT_8061abc0, DOUBLE_8061ab70, ctx, (long long)px,
                (double)FLOAT_8061ab30, (double)FLOAT_80611a70, (double)FLOAT_80611a64, 0,
                (unsigned)px, py, icon, name, wrap, 1, 0, (int*)0);
            unsigned yAcc = py + adv;
            text_set_font_table(0x12);
            char* fmtSrc = name;
            if (label != (char*)0) {
                int adv2 = aveditor_draw_tooltip_box(ctx, DOUBLE_8061ab70, ctx, (long long)px,
                    (double)FLOAT_8061ab30, (double)FLOAT_80611a70, (double)FLOAT_80611a64, 0,
                    (unsigned)px, yAcc, -1, label, wrap, 0, 0, (int*)0);
                yAcc += adv2;
                fmtSrc = label;
            }
            int fmt = get_string_by_id(0xd1d);
            unsigned msg = debug_log_formatted((unsigned long long)ctx, DOUBLE_8061ab70, ctx, (long long)px,
                (double)FLOAT_8061ab30, (double)FLOAT_80611a70, (double)FLOAT_80611a64, 0, fmt,
                count, icon, fmtSrc, wrap, 1, 0, (int*)0);
            double dy = (double)(float)((double)CONCAT44(0x43300000, yAcc ^ 0x80000000) - DOUBLE_8061ab70);
            double dx = (double)(float)((double)CONCAT44(0x43300000, (unsigned)px ^ 0x80000000) - DOUBLE_8061ab70);
            text_draw_char_shadowed(dx, dy, msg, 0x5050507f, 0x40);
        }
    } else {
        int pad = DAT_80612d68[0x24];
        int panel = *(int*)(DAT_80612be4 + 0x200);
        text_set_font_table(4);
        int px = (int)((double)FLOAT_80611a68 *
            (double)(float)((double)FLOAT_8061ab30 + (double)*(float*)(&DAT_8051a8f8 + DAT_806119ec * 0x1e0)) +
            (double)FLOAT_80611a70);
        int py = (int)((double)FLOAT_8061abc0 * (double)FLOAT_80611a64 +
            (double)(float)((double)FLOAT_8061ab30 * (double)FLOAT_80611a64 + (double)FLOAT_80611a6c));
        unsigned wrap = (unsigned)((double)(float)((double)FLOAT_8061abc4 * (double)FLOAT_80611a68 + (double)FLOAT_80611a70) -
            (double)(float)((double)(CONCAT44(0x43300000, (unsigned)px) ^ 0x80000000) - DOUBLE_8061ab70));
        int icon = (panel == 0) ? -1 : (int)(unsigned char)(&DAT_8047c14c)[pad * 0x140];
        char* name = (char*)get_string_by_id(*(int*)(&DAT_8047c150 + DAT_80612d68[0x24] * 0x140));
        aveditor_draw_tooltip_box(DOUBLE_8061ab70, ctx, ctx, (long long)px,
            (double)FLOAT_80611a64, (double)FLOAT_80611a70, ctx, 0,
            (unsigned)px, py, icon, name, wrap, 1, 0, (int*)0);
    }
}

// Full preview-panel draw. Sets up the preview viewport + scissor from the fitted rect
// (DAT_80612d68 words 1..8), then in drag-region mode draws each fully-placed / fading brush segment,
// in single-tile mode draws the cursor entity with its animated highlight (or the placement spark
// particle for a particle cursor), draws the tooltip/info boxes, and finally the preview entities.
void aveditor_preview_draw(void)
{
    FUN_803826dc();
    int savedBlend = gx_set_blend_mode(-1);

    if (*DAT_80612d68 == 0) { FUN_80382728(); return; }
    int hw = half_round(DAT_80612d68[3]);
    int hh = half_round(DAT_80612d68[4]);
    if (!((DAT_80612d68[1] - hw) + 1 < DAT_80612d68[1] + hw) ||
        !((DAT_80612d68[2] - hh) + 1 < DAT_80612d68[2] + hh)) {
        FUN_80382728();
        return;
    }

    viewport_save_active();
    viewport_set_rect_active((DAT_80612d68[1] - hw) + 1, (DAT_80612d68[2] - hh) + 1,
                             DAT_80612d68[1] + hw, DAT_80612d68[2] + hh);
    if ((*(unsigned*)(DAT_80612be4 + 0x25c) & 0x2000000) != 0) {
        int bw = half_round(DAT_80612d68[7]);
        int bh = half_round(DAT_80612d68[8]);
        sprite_draw_bordered_frame(DAT_80612d68[5] - bw, (DAT_80612d68[6] - bh) + 1,
                                   DAT_80612d68[5] + bw, DAT_80612d68[6] + bh, 0x78);
    }
    int sw = half_round(DAT_80612d68[7]);
    int sh = half_round(DAT_80612d68[8]);
    gx_set_scissor_rect((unsigned)((DAT_80612d68[5] - sw) + 1), (unsigned)((DAT_80612d68[6] - sh) + 1),
                        DAT_80612d68[5] + sw, DAT_80612d68[6] + sh);
    double depthBase = (double)(FLOAT_8061ab94 + FLOAT_8060fad0);
    viewport_set_depth_params_active(depthBase, -depthBase, (double)FLOAT_8061ab98);
    double ctx = viewport_set_gx_active();

    // --- drag-region mode: render the queued brush segments (two passes) ---
    if (-1 < DAT_80612d68[0x25]) {
        float hx = DAT_803e0acc, hy = DAT_803e0ad0, hz = DAT_803e0ad4;
        float len = hz * hz + hx * hx + hy * hy;
        if (FLOAT_8061ab9c < len) {
            float r = 1.0f / (float)math_sqrt((double)len);
            r = r * -(FLOAT_8061ab2c * len * r * r - FLOAT_8061ab58);
            float sc = FLOAT_8061ab84 * r * -(FLOAT_8061ab2c * len * r * r - FLOAT_8061ab58);
            hx *= sc; hy *= sc; hz *= sc;
        }
        gx_set_blend_mode(4);
        gx_set_sub_material_id(0);
        // pass 1: fully-placed segments (fade counter > 0x7e)
        for (int i = 0, off = 0; i < DAT_80612d68[0x796a]; i++, off += 0x78) {
            if (0x7e < *(int*)((int)DAT_80612d68 + off + 0x5cc)) {
                ctx = draw_brush_segment(off, ctx);
            }
        }
        // pass 2: mid-fade segments (0x18 < counter < 0x7f)
        for (int i = 0, off = 0; i < DAT_80612d68[0x796a]; i++, off += 0x78) {
            int f = *(int*)((int)DAT_80612d68 + off + 0x5cc);
            if (0x18 < f && f < 0x7f) {
                ctx = draw_brush_segment(off, ctx);
            }
        }
        gx_set_sub_material_id(1);
        gx_set_blend_mode(10);
    }

    if (DAT_80612d68[0x24] < 0) {
        // --- single-tile / particle cursor render ---
        int cell = DAT_80612d68[0x22];
        if (-1 < cell && DAT_80612d68[0x21] != 0) {
            int cursorEnt = *(int*)(DAT_80612be4 + 0x4150);
            if (*(short*)(cursorEnt + 0x94) == 0x798 && *(int*)(DAT_80612be4 + 0x254) == 1) {
                draw_placement_spark(cursorEnt);
            } else if (cell == 0x46 || cell == 0x47) {
                int ent2 = *(int*)(DAT_80612be4 + 0x4154);
                gx_set_blend_mode(4);
                unsigned long long r = render_instance_draw_multipass(ctx, ctx, *(int*)(cursorEnt + 4));
                gx_set_blend_mode(4);
                render_instance_draw_multipass((double)r, ctx, *(int*)(ent2 + 4));
            } else {
                gx_set_blend_mode(4);
                render_instance_draw_multipass(ctx, ctx, *(int*)(cursorEnt + 4));
            }
        }
    } else {
        // --- placed-tile highlight render ---
        int pad = DAT_80612d68[0x24];
        if ((DAT_80612d68[0x20] & 4) == 0) {
            float hx = DAT_803e0ad8, hy = DAT_803e0adc, hz = DAT_803e0ae0;
            int t = aveditor_get_pad_tile_count(pad);
            int te = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
            *(unsigned*)(te + 0x28) |= 0x200008;
            if (*(int*)(DAT_80612be4 + 0x2f7c) != 0) {
                color_expand_7bit(aveditor_tick_pulse());
            } else if (*(int*)(DAT_80612be4 + 0x2f18) != 0) {
                color_expand_7bit(aveditor_tick_pulse());
            }
            float len = hz * hz + hx * hx + hy * hy;
            if (FLOAT_8061ab9c < len) {
                float r = 1.0f / (float)math_sqrt((double)len);
                r = r * -(FLOAT_8061ab2c * len * r * r - FLOAT_8061ab58);
                float sc = FLOAT_8061ab84 * r * -(FLOAT_8061ab2c * len * r * r - FLOAT_8061ab58);
                hx *= sc; hy *= sc; hz *= sc;
            }
            render_entity_submit_multipass_indexed((unsigned long long)ctx, ctx);
        } else {
            int t = aveditor_get_pad_tile_count(pad);
            int te = *(int*)(DAT_80612be4 + t * 4 + 0x4068);
            *(unsigned*)(te + 0x28) |= 0x200008;
            render_entity_submit_multipass((unsigned long long)ctx, ctx);
        }
        gx_set_sub_material_id(1);
        gx_set_blend_mode(10);
    }

    // --- tooltip / info text boxes ---
    if ((DAT_80612d68[0x20] & 0x40) != 0) {
        draw_info_tooltips(ctx);
    }

    // --- preview entities ---
    if (-1 < DAT_80612d68[0x24] && (DAT_80612d68[0x20] & 0x10) != 0) {
        aveditor_preview_render((unsigned long long)ctx, ctx);
    }

    gx_set_sub_material_id(1);
    gx_set_blend_mode(savedBlend);
    viewport_swap_active_saved();
    viewport_set_gx_active();
    aveditor_preview_render_particles();
    FUN_80382728();
}

