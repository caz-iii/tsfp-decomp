// Arcade select-screen intro / transition graphics + timing.
//
// Batch: batch_832
// Module: the animated intro overlay for the arcade select screen (spark FX,
//   timed screen advance, and the textured "flash / panel" quad batches drawn
//   through the GX FIFO). Companion to arcade_select_screen.cpp (batch_830/831).
//
//   The two big quad emitters (arcsel_fx_draw_quads_a/b) push vertex data
//   directly to the GX command-processor FIFO write-gather port at 0xCC008000
//   (DAT_cc008000) in GX_DRAW mode 5 (quads). Subsystem: graphics.
//
// Non-matching decomp. Behavior-faithful; the long unrolled FIFO stores are
// preserved verbatim so the emitted vertex stream matches the original.

#include "common.h"

// GX FIFO write-gather port (command processor fifo). Writing a float or a
// u32 here appends it to the current primitive's vertex stream.
extern volatile float DAT_cc008000;      // as float store
#define GXFIFO_F(v)  (DAT_cc008000 = (float)(v))
#define GXFIFO_U(v)  (*(volatile unsigned int *)&DAT_cc008000 = (unsigned int)(v))

// ---- Per-file externs (raw symbols) ----
extern int   input_get_active_count(void);
extern uint  input_get_buttons_held(int idx);
extern void  input_reset_all_pads(void);
extern void  hud_element_init_from_layout(int a, int b, int c, int d);
extern void  hud_screen_push(void);
extern int   hud_element_is_closing(int elem);
extern void  mode_hud_screen_push_once(void);
extern void  spark_fx_init_params(float *params);
extern void  sound_init_play_params(int params);
extern void  sound_play_request(int *params);
extern int   sprite_get_texture_id(int slot);
extern void  gx_begin_batch_mode5(int a, int b, int nVerts, int texId);
extern void  gx_end_batch(void);
extern void  gx_set_sub_material_id(int id);
extern void  gx_set_color_update_mode(void);
extern void  gx_set_blend_mode(int mode);
extern int   gx_set_shadow_enable(int on);
extern void  gx_set_shadow_intensity(double v);
extern void  gx_bind_material_by_index_w(int idx);
extern void  gx_draw_box_outline(int x0, int y0, int x1, int y1, int a, int b, int c, int d, int e, int f);
extern void  gx_fill_rect_flags(int flags, int x0, int y0, int x1, int y1, int a);
extern int   FUN_803825c0(double v);
extern u64   FUN_803826dc(void);
extern u64   FUN_803826e4(void);
extern void  FUN_80382728(void);
extern void  FUN_80382730(void);
extern u64   __save_gpr_25(void);
extern void  __restore_gpr_25(void);

// Raw data symbols.
extern int  *DAT_80610a20;      // sprite/texture descriptor table base
extern int   DAT_806109c0;      // frame-rate scale
extern int   DAT_806109c4;      // screen width
extern int   DAT_806109c8;      // screen height
extern int   DAT_80612b64;      // spark-fx owner object ptr
extern float FLOAT_80612b48;    // secondary transition timer
extern float DAT_805cc6dc;      // intro transition timer
extern int   DAT_805cc6e0;      // intro transition phase (-1..2)

extern float  FLOAT_806108e8;
extern float  FLOAT_806122c4;
extern float  FLOAT_806122c8;
extern float  FLOAT_8061a180;
extern float  FLOAT_8061a184;
extern float  FLOAT_8061a188;
extern float  FLOAT_8061a198;
extern float  FLOAT_8061a19c;
extern float  FLOAT_8061a1a0;
extern float  FLOAT_8061a1a4;
extern float  FLOAT_8061a1a8;
extern float  FLOAT_8061a1ac;
extern float  FLOAT_8061a1b0;
extern float  FLOAT_8061a1b4;
extern float  FLOAT_8061a1c8;
extern double DOUBLE_8061a190;
extern double DOUBLE_8061a1b8;
extern double DOUBLE_8061a1c0;

// Helpers to mirror the decompiler's (double)CONCAT44(0x43300000, x^0x80000000) - bias
// idiom, which is the PPC signed-int-to-float magic-number conversion.
static inline float i2f(int x) { return (float)x; }

// byteswap a 32-bit color (the FIFO expects big-endian RGBA words)
static inline unsigned int bswap32(unsigned int c) {
    return (c << 24) | ((c & 0xff00) << 8) | ((c >> 8) & 0xff00) | (c >> 24);
}

// ---------------------------------------------------------------------------
// arcsel_intro_any_button_held (0x802e26f8)
//   Returns 1 if any active pad has all of the given button bits held.
// ---------------------------------------------------------------------------
unsigned int arcsel_intro_any_button_held(uint mask)
{
    int i = 0;
    while (i < input_get_active_count()) {
        if ((mask & input_get_buttons_held(i)) != 0) {
            return 1;
        }
        i = i + 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// arcsel_intro_start (0x802e275c)
//   Resets the intro transition timer/phase and seeds the spark FX params.
// ---------------------------------------------------------------------------
void arcsel_intro_start(void)
{
    DAT_805cc6dc = FLOAT_8061a180;
    DAT_805cc6e0 = 0;
    spark_fx_init_params((float *)(DAT_80612b64 + 0x654));
}

// ---------------------------------------------------------------------------
// arcsel_intro_tick_secondary (0x802e279c)
//   Advances the secondary intro timer and, once past threshold and not
//   already closing, pushes the screen once.
// ---------------------------------------------------------------------------
void arcsel_intro_tick_secondary(int elem)
{
    FLOAT_80612b48 = FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0)) + FLOAT_80612b48;
    if ((hud_element_is_closing(elem) == 0) && (FLOAT_8061a188 < FLOAT_80612b48)) {
        mode_hud_screen_push_once();
    }
}

// ---------------------------------------------------------------------------
// arcsel_intro_element_init (0x802e2810)
//   Initializes the intro element and sets its flag bits (+0x40).
// ---------------------------------------------------------------------------
void arcsel_intro_element_init(int elem)
{
    hud_element_init_from_layout(0, 0, 0, 0);
    *(uint *)(elem + 0x40) = *(uint *)(elem + 0x40) & 0xfffffffd;
    *(uint *)(elem + 0x40) = *(uint *)(elem + 0x40) | 0x40;
}

// ---------------------------------------------------------------------------
// arcsel_intro_tick (0x802e2868)
//   Main intro transition state machine (phases -1..2): advances the timer,
//   plays the confirm sound, and steps through the intro -> ready sequence.
// ---------------------------------------------------------------------------
void arcsel_intro_tick(void)
{
    int soundParams[12];

    DAT_805cc6dc = DAT_805cc6dc + FLOAT_806108e8 * (FLOAT_8061a184 / i2f(DAT_806109c0));

    if (DAT_805cc6e0 == 1) {
        if ((arcsel_intro_any_button_held(0x180) != 0) || (FLOAT_8061a1a0 <= DAT_805cc6dc)) {
            input_reset_all_pads();
            DAT_805cc6e0 = 2;
        }
    } else if ((DAT_805cc6e0 < 1) && (-1 < DAT_805cc6e0)) {
        if (arcsel_intro_any_button_held(0x180) == 0) {
            if (FLOAT_8061a19c <= DAT_805cc6dc) {
                sound_init_play_params((int)soundParams);
                soundParams[0] = 0xd28;
                sound_play_request(soundParams);
                DAT_805cc6e0 = 1;
                hud_screen_push();
            }
        } else {
            DAT_805cc6e0 = 2;
            mode_hud_screen_push_once();
        }
    }
}

// ---------------------------------------------------------------------------
// arcsel_fx_draw_quads_a (0x802e2988)
//   Draws the two-texture (slots 0xf, 0x12) animated flash/panel quad batch
//   with the fade color computed from the transition timer. Emits GX_DRAW
//   mode-5 quads directly to the FIFO.
// ---------------------------------------------------------------------------
void arcsel_fx_draw_quads_a(u32 param_1, u32 param_2, uint param_3, uint param_4, int param_5)
{
    (void)param_1; (void)param_2;
    float fVar1, fVar2, fVar3, fVar4, fVar5, fVar6;
    short sVar7, sVar8;
    uint uVar9, uVar10, uVar12, uVar13, uVar14, uVar16;
    int iVar11, iVar15;
    double f1;
    u64 hi;

    hi = FUN_803826dc();
    uVar13 = (uint)((u64)hi >> 0x20);
    uVar12 = (uint)hi;
    f1 = (double)FLOAT_8061a1a4;   // scaled by extraout f1 in orig; timer-driven fade alpha
    uVar10 = FUN_803825c0((double)(float)(f1));

    uVar16 = uVar13 + param_5;
    uVar9  = uVar12 + param_5;
    uVar14 = param_3 - param_5;
    fVar1 = i2f((int)uVar16);
    fVar2 = i2f((int)uVar12);
    fVar3 = i2f((int)uVar13);
    fVar4 = i2f((int)uVar9);
    double dVar34 = (double)i2f((int)uVar16);
    double dVar33 = (double)i2f((int)uVar9);
    fVar5 = i2f((int)uVar14);
    fVar6 = i2f((int)uVar12);
    double dVar32 = (double)i2f((int)param_3);
    double dVar31 = (double)i2f((int)uVar9);
    double dVar30 = (double)i2f((int)uVar14);
    double dVar29 = (double)i2f((int)uVar9);
    double dVar28 = (double)i2f((int)uVar16);
    uVar9 = param_4 - param_5;
    double dVar27 = (double)i2f((int)param_4);
    double dVar26 = (double)i2f((int)uVar13);
    double dVar25 = (double)i2f((int)uVar9);
    double dVar24 = (double)i2f((int)uVar16);
    double dVar23 = (double)i2f((int)uVar9);
    double dVar22 = (double)i2f((int)uVar14);
    double dVar21 = (double)i2f((int)param_4);
    double dVar20 = (double)i2f((int)param_3);
    double dVar19 = (double)i2f((int)uVar9);
    double dVar18 = (double)i2f((int)uVar14);
    double dVar17 = (double)i2f((int)uVar9);

    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_set_color_update_mode();

    iVar11 = sprite_get_texture_id(0xf);
    iVar15 = *DAT_80610a20 + iVar11 * 0x58;
    sVar7 = *(short *)(iVar15 + 0x4e);
    sVar8 = *(short *)(iVar15 + 0x52);
    gx_begin_batch_mode5(0x92, 2, 0x10, iVar11);

    unsigned int cHi = (uVar10 & 0xff) | 0x7f7f7f00;
    unsigned int cLo = (uVar10 & 0xff) | 0x28282800;
    uVar14 = (uint)(int)(short)(sVar7 << 4);
    uVar9  = (uint)(int)(short)(sVar8 << 4);

    // batch A verts (texture slot 0xf)
    GXFIFO_F(i2f((int)uVar13)); GXFIFO_F(i2f((int)uVar12)); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar1); GXFIFO_F(fVar2); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar34); GXFIFO_F((float)dVar33); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(fVar3); GXFIFO_F(fVar4); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar32); GXFIFO_F((float)dVar31); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(i2f((int)param_3)); GXFIFO_F(i2f((int)uVar12)); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar5); GXFIFO_F(fVar6); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar30); GXFIFO_F((float)dVar29); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar26); GXFIFO_F((float)dVar25); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar24); GXFIFO_F((float)dVar23); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar28); GXFIFO_F((float)dVar27); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(i2f((int)uVar13)); GXFIFO_F((float)dVar21); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar18); GXFIFO_F((float)dVar17); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar20); GXFIFO_F((float)dVar19); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(i2f((int)param_3)); GXFIFO_F((float)dVar21); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar14) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar22); GXFIFO_F((float)dVar21); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    gx_end_batch();

    iVar11 = sprite_get_texture_id(0x12);
    iVar15 = *DAT_80610a20 + iVar11 * 0x58;
    sVar7 = *(short *)(iVar15 + 0x4e);
    sVar8 = *(short *)(iVar15 + 0x52);
    gx_begin_batch_mode5(0x92, 2, 0x10, iVar11);
    uVar13 = (uint)(int)(short)(sVar7 << 4);
    uVar9  = (uint)(int)(short)(sVar8 << 4);

    GXFIFO_F(fVar1); GXFIFO_F(fVar2); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar5); GXFIFO_F(fVar6); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar30); GXFIFO_F((float)dVar29); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar34); GXFIFO_F((float)dVar33); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar24); GXFIFO_F((float)dVar23); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar18); GXFIFO_F((float)dVar17); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar22); GXFIFO_F((float)dVar21); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar28); GXFIFO_F((float)dVar27); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar3); GXFIFO_F(fVar4); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar34); GXFIFO_F((float)dVar33); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar24); GXFIFO_F((float)dVar23); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar26); GXFIFO_F((float)dVar25); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar30); GXFIFO_F((float)dVar29); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar32); GXFIFO_F((float)dVar31); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cLo);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar20); GXFIFO_F((float)dVar19); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar18); GXFIFO_F((float)dVar17); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(cHi);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    gx_end_batch();

    FUN_80382728();
}

// ---------------------------------------------------------------------------
// arcsel_fx_draw_quads_b (0x802e3620)
//   Second quad emitter (texture slots 0x10, 0x11) with a caller-supplied
//   packed color; used by arcsel_fx_draw_panel for the outlined flash box.
// ---------------------------------------------------------------------------
void arcsel_fx_draw_quads_b(u32 param_1, u32 param_2, uint param_3, uint param_4, int param_5, uint colorArg)
{
    (void)param_1; (void)param_2;
    float fVar1, fVar2, fVar3, fVar4, fVar5, fVar6;
    short sVar7, sVar8;
    uint uVar9, uVar12, uVar13, uVar14, uVar16;
    int iVar10, iVar14n;
    u64 hi;

    hi = __save_gpr_25();
    uVar13 = (uint)((u64)hi >> 0x20);
    uVar12 = (uint)hi;
    uVar16 = uVar13 + param_5;
    uVar9  = uVar12 + param_5;
    fVar1 = i2f((int)uVar16);
    uVar14 = param_3 - param_5;
    fVar2 = i2f((int)uVar12);
    fVar3 = i2f((int)uVar13);
    fVar4 = i2f((int)uVar9);
    double dVar33 = (double)i2f((int)uVar16);
    double dVar32 = (double)i2f((int)uVar9);
    fVar5 = i2f((int)uVar14);
    fVar6 = i2f((int)uVar12);
    double dVar31 = (double)i2f((int)param_3);
    double dVar30 = (double)i2f((int)uVar9);
    double dVar29 = (double)i2f((int)uVar14);
    double dVar28 = (double)i2f((int)uVar9);
    double dVar27 = (double)i2f((int)uVar16);
    double dVar26 = (double)i2f((int)param_4);
    uVar9 = param_4 - param_5;
    double dVar25 = (double)i2f((int)uVar13);
    double dVar24 = (double)i2f((int)uVar9);
    double dVar23 = (double)i2f((int)uVar16);
    double dVar22 = (double)i2f((int)uVar9);
    double dVar21 = (double)i2f((int)uVar14);
    double dVar20 = (double)i2f((int)param_4);
    double dVar19 = (double)i2f((int)param_3);
    double dVar18 = (double)i2f((int)uVar9);
    double dVar17 = (double)i2f((int)uVar14);
    double dVar16d = (double)i2f((int)uVar9);

    unsigned int col = bswap32(colorArg);

    iVar10 = sprite_get_texture_id(0x10);
    iVar14n = *DAT_80610a20 + iVar10 * 0x58;
    sVar7 = *(short *)(iVar14n + 0x4e);
    sVar8 = *(short *)(iVar14n + 0x52);
    gx_begin_batch_mode5(0x92, 2, 0x10, iVar10);
    uVar13 = (uint)(int)(short)(sVar7 << 4);
    uVar9  = (uint)(int)(short)(sVar8 << 4);

    GXFIFO_F(i2f((int)((uint)((u64)hi >> 0x20)))); GXFIFO_F(i2f((int)((uint)hi))); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar1); GXFIFO_F(fVar2); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar33); GXFIFO_F((float)dVar32); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(fVar3); GXFIFO_F(fVar4); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar31); GXFIFO_F((float)dVar30); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(fVar5); GXFIFO_F(fVar6); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar29); GXFIFO_F((float)dVar28); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar25); GXFIFO_F((float)dVar24); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar23); GXFIFO_F((float)dVar22); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar27); GXFIFO_F((float)dVar26); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(i2f((int)uVar13)); GXFIFO_F((float)dVar20); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar17); GXFIFO_F((float)dVar16d); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar19); GXFIFO_F((float)dVar18); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar21); GXFIFO_F((float)dVar20); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    gx_end_batch();

    iVar10 = sprite_get_texture_id(0x11);
    iVar14n = *DAT_80610a20 + iVar10 * 0x58;
    sVar7 = *(short *)(iVar14n + 0x4e);
    sVar8 = *(short *)(iVar14n + 0x52);
    gx_begin_batch_mode5(0x92, 2, 0x14, iVar10);
    uVar13 = (uint)(int)(short)(sVar7 << 4);
    uVar9  = (uint)(int)(short)(sVar8 << 4);

    GXFIFO_F((float)dVar33); GXFIFO_F((float)dVar32); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar29); GXFIFO_F((float)dVar28); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar17); GXFIFO_F((float)dVar16d); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar23); GXFIFO_F((float)dVar22); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F(fVar1); GXFIFO_F(fVar2); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar5); GXFIFO_F(fVar6); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar29); GXFIFO_F((float)dVar28); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar33); GXFIFO_F((float)dVar32); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar23); GXFIFO_F((float)dVar22); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar17); GXFIFO_F((float)dVar16d); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar21); GXFIFO_F((float)dVar20); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar27); GXFIFO_F((float)dVar26); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F(fVar3); GXFIFO_F(fVar4); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar33); GXFIFO_F((float)dVar32); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar23); GXFIFO_F((float)dVar22); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    GXFIFO_F((float)dVar19); GXFIFO_F((float)dVar18); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(FLOAT_8061a180 * FLOAT_806122c4);
    GXFIFO_F((float)dVar31); GXFIFO_F((float)dVar30); GXFIFO_F(FLOAT_8061a180); GXFIFO_U(col);
    GXFIFO_F(i2f((int)uVar13) * FLOAT_806122c8); GXFIFO_F(i2f((int)uVar9) * FLOAT_806122c4);
    gx_end_batch();

    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// arcsel_fx_draw_flash (0x802e3580)
//   Computes the timer-driven fade alpha for an element and draws the "flash"
//   quads (arcsel_fx_draw_quads_a) at the element's rect (+0xe0..+0xec).
// ---------------------------------------------------------------------------
void arcsel_fx_draw_flash(int elem)
{
    float alpha = FLOAT_8061a1b0;
    if (*(float *)(elem + 0xb0) < FLOAT_8061a1b4) {
        alpha = *(float *)(elem + 0xb0) * FLOAT_8061a1ac;
    }
    arcsel_fx_draw_quads_a((int)*(float *)(elem + 0xe0), (int)*(float *)(elem + 0xe4),
                           (int)(*(float *)(elem + 0xe8) - FLOAT_8061a1a8),
                           (int)(*(float *)(elem + 0xec) - FLOAT_8061a1a8), (int)alpha);
}

// ---------------------------------------------------------------------------
// arcsel_fx_draw_panel (0x802e42c0)
//   Draws the outlined shadowed flash panel: two color-modulated quad passes
//   (arcsel_fx_draw_quads_b) around a shadowed box outline + filled rect.
// ---------------------------------------------------------------------------
void arcsel_fx_draw_panel(u32 param_1, u32 param_2, uint param_3, uint param_4, int param_5)
{
    (void)param_1; (void)param_2;
    int  hi, lo;
    int  colByte;
    int  shadowPrev;
    double f1;
    u64  ctx;

    ctx = FUN_803826e4();
    hi = (int)((u64)ctx >> 0x20);
    lo = (int)ctx;
    f1 = (double)FLOAT_8061a1a4;   // timer-driven fade factor

    gx_set_sub_material_id(1);
    gx_set_color_update_mode();
    colByte = FUN_803825c0((double)(float)((double)FLOAT_8061a1a4 * f1));
    arcsel_fx_draw_quads_b(hi, lo, param_3, param_4, param_5, (uint)(colByte << 0x18));

    gx_set_sub_material_id(1);
    gx_set_color_update_mode();
    shadowPrev = gx_set_shadow_enable(1);
    gx_set_shadow_intensity(DOUBLE_8061a1b8);
    gx_bind_material_by_index_w(-6);
    gx_set_shadow_intensity(DOUBLE_8061a1c0);
    gx_set_shadow_enable(shadowPrev);
    gx_draw_box_outline(hi, lo, param_3, param_4, hi << 4, lo << 4, param_3 << 4, param_4 << 4, 1, 3);
    gx_fill_rect_flags(8, hi, lo, param_3, param_4, 0);

    colByte = FUN_803825c0((double)(float)((double)FLOAT_8061a198 * f1));
    arcsel_fx_draw_quads_b(hi, lo, param_3, param_4, param_5, (uint)(colByte << 0x18));

    gx_set_sub_material_id(1);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// arcsel_fx_draw_panel_clamped (0x802e4428)
//   Clamps an element's rect (+0xe0..+0xec) to screen bounds and draws the
//   flash panel with a timer-driven alpha.
// ---------------------------------------------------------------------------
void arcsel_fx_draw_panel_clamped(int elem)
{
    uint x0 = (uint)*(float *)(elem + 0xe0);
    uint y0 = (uint)*(float *)(elem + 0xe4);
    uint x1 = (uint)(*(float *)(elem + 0xe8) - FLOAT_8061a1a8);
    uint y1 = (uint)(*(float *)(elem + 0xec) - FLOAT_8061a1a8);
    float alpha = FLOAT_8061a1b0;
    if (*(float *)(elem + 0xb0) < FLOAT_8061a1c8) {
        alpha = *(float *)(elem + 0xb0) * FLOAT_8061a1ac - FLOAT_8061a184;
    }

    // clamp each coord: if negative -> 0, else min(coord, screen dim)
    uint cx0 = ((int)x0 < 0) ? 0 : ((int)x0 < (int)DAT_806109c4 ? x0 : (uint)DAT_806109c4);
    uint cx1 = ((int)x1 < 0) ? 0 : ((int)x1 < (int)DAT_806109c4 ? x1 : (uint)DAT_806109c4);
    uint cy0 = ((int)y0 < 0) ? 0 : ((int)y0 < (int)DAT_806109c8 ? y0 : (uint)DAT_806109c8);
    uint cy1 = ((int)y1 < 0) ? 0 : ((int)y1 < (int)DAT_806109c8 ? y1 : (uint)DAT_806109c8);

    arcsel_fx_draw_panel(cx0, cy0, cx1, cy1, (int)alpha);
}
