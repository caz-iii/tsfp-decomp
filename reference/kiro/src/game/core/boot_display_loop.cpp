// ============================================================================
// boot_display_loop.cpp | batch_501 | Date: 2026-09-06
// Subsystem: game/core (boot-time "display the game?" consent screen loop).
//
//   game_main_loop     0x80181068  boot-time display-consent (EULA-style) screen loop
//   halt_stub_assert   0x80181a70  empty stub (assert/halt placeholder, 4 bytes)
//
// game_main_loop is the boot-time confirmation screen invoked once during startup
// (gated by boot_skip_requested() && *DAT_80612398==0).  It renders a two-line
// prompt plus a Yes / No pair inside a drawn box, runs a ~10000-tick input loop
// that lets the player toggle the selection with up/down (button bit 1 / bit 2 or
// the analog stick) and confirm, then either arms progressive video via
// gx_init_render_mode (Yes) or leaves it (No).  A second identical loop displays a
// three-line follow-up screen.  Between frames it polls the disc-error / message
// screen (FUN_803a4db4 -> boot_render_message_screen).
//
// Non-matching: the decompiler surfaced many float spill temporaries (f28..f31 and
// their paired-single halves) that never feed the integer control flow; they are
// dropped.  Screen-centre maths that the decompiler expressed via the 2^52 double
// bias trick are written as plain integer halving.  Text pointers come from the
// PTR_* table at 0x80412608.
// ============================================================================
#include "common.h"

extern "C" {
// ---- frame / render helpers (named cross-module; declared here for arity) ----
extern void  FUN_803826b8(void);                 // profiler push
extern void  FUN_80382704(void);                 // profiler pop
extern int   FUN_8038b5c8(int strPtr);           // measured text width (in 0x10 units)
extern unsigned long long FUN_8039ec70(char progressive);  // arm video field mode
extern int   FUN_803a4db4(void);                 // poll boot message/disc-error state
extern void  FUN_803c94d8(void);                 // flush pending GX copy

extern void  boot_render_message_screen(void);
extern int   boot_skip_requested(void);
extern void  font_render_string(int x, int y, int size, const char *text);
extern unsigned long long gx_draw_line_i(int x0, int y0, int x1, int y1, unsigned int color);
extern unsigned long long gx_fill_rect_clipped(int x0, int y0, int x1, int y1, unsigned int color);
extern void  gx_frame_flush_wait(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                 unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                 unsigned long long a8);
extern void  gx_init_render_mode(unsigned long long p1, double p2, double p3, unsigned long long p4,
                                 unsigned long long p5, unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8, int forceProgressive);
extern void  gx_post_present_setup(void);
extern void  gx_reset_render_state(void);
extern int   gx_set_blend_mode(int mode);
extern void  gx_set_scissor_rect(int a, int b, int c, int d);
extern int   gx_set_sub_material_id(int id);
extern double input_get_analog_value(int pad, int axis);
extern unsigned int input_get_buttons_held(int pad);
extern unsigned int input_get_buttons_pressed(int pad);
extern void  input_update_all(void);
extern unsigned long long mem_heap_init(void);
extern void  mem_heap_reset_counters(void);
extern void  occ_query_flush_guard(void);
extern int   os_get_time_base(void);
extern void  particle_set_pool_sizes(void);
extern void  render_target_pool_alloc(unsigned long long a1, double a2, double a3,
                                      unsigned long long a4, unsigned long long a5,
                                      unsigned long long a6, unsigned long long a7,
                                      unsigned long long a8);
extern void  screen_scale_update(void);
extern unsigned long long vi_present_frame(int a);
extern void  viewport_reset_default_bounds(void);
extern void  viewport_set_active(void *rt);
extern void  viewport_set_depth_params_active(double a, double b, double c);
extern void  viewport_set_gx_active(void);
extern void  viewport_set_rect_active(int x, int y, int w, int h);

// ---- data / state ----
extern int   DAT_806109c4;      // framebuffer width
extern int   DAT_806109c8;      // framebuffer height
extern int  *DAT_80612398;      // ptr to boot-phase flag
extern int   DAT_80611748[];    // scissor rect array
extern int   DAT_800000f8;      // CPU bus clock (for tick->ms)
extern float FLOAT_80616688, FLOAT_806166dc;             // analog thresholds (down / up)
extern float FLOAT_8061668c, FLOAT_80616690, FLOAT_8060fad0;  // depth params
extern float FLOAT_806166e0, FLOAT_806166e8;             // box top offsets
extern float FLOAT_806166ec, FLOAT_806166f0;             // box bottom offsets

// text-pointer table: [0..1] = two prompt lines, [4],[5],[7] follow-up lines,
// PTR_DAT_80412610 = "Yes"/prompt tail, PTR_DAT_80412614 = "No".
extern char *PTR_s_Do_you_want_to_display_the_game_80412608;
extern char *PTR_DAT_80412610;
extern char *PTR_DAT_80412614;
} // extern "C"

// Screen-centre helpers (integer halving with round-toward-zero, matching the
// decompiler's (x>>1)+(x<0 && x&1) idiom).
static inline int half(int v) { return (v >> 1) + ((v < 0 && (v & 1) != 0) ? 1 : 0); }

// game_main_loop (0x80181068)
void game_main_loop(void)
{
    FUN_803826b8();

    if (boot_skip_requested() == 0 || (DAT_80612398 != 0 && *DAT_80612398 != 0)) {
        FUN_80382704();
        return;
    }

    // ticks-per-ms divisor used by the timeout loops.
    const unsigned int msDiv = (unsigned int)(DAT_800000f8 / 4000);
    char yesSelected = 1;

    int rtBuf[18];  // render-target descriptor scratch (auStack_f8, 0x48 bytes)

    int boxTop    = half(DAT_806109c8) * -1;    // -(centreY)
    (void)boxTop;
    int lineTopY  = (int)(-(float)half(DAT_806109c8) - FLOAT_806166e8);
    int lineBotY  = (int)(-(float)half(DAT_806109c8) + FLOAT_806166ec);

    render_target_pool_alloc(mem_heap_init(), 0, 0, 0, 0, 0, 0, 0);
    viewport_set_active(rtBuf);
    viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
    viewport_set_depth_params_active((double)(FLOAT_8061668c + FLOAT_8060fad0),
                                     -(double)(FLOAT_8061668c + FLOAT_8060fad0),
                                     (double)FLOAT_80616690);
    viewport_set_gx_active();
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);

    // ---- first screen: two-line prompt + Yes/No ---------------------------
    int startTick = os_get_time_base();
    input_update_all();
    int minX = DAT_806109c4, maxX = 0;

    while (((unsigned int)(os_get_time_base() - startTick) / msDiv < 10000) &&
           (input_get_buttons_pressed(0) & 0x80) == 0 &&
           (input_get_buttons_pressed(1) & 0x80) == 0 &&
           (input_get_buttons_pressed(2) & 0x80) == 0 &&
           (input_get_buttons_pressed(3) & 0x80) == 0) {
        int textY = (int)(-(float)half(DAT_806109c8) - FLOAT_806166e8);
        int noWidth = FUN_8038b5c8((int)&PTR_DAT_80412610);
        int noCentreX = half(DAT_806109c4 - noWidth * 0x10);

        occ_query_flush_guard();
        gx_post_present_setup();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);

        // two prompt lines
        char **prompt = &PTR_s_Do_you_want_to_display_the_game_80412608;
        int rowY = textY;
        for (int i = 0; i < 2; ++i) {
            int w = FUN_8038b5c8((int)prompt[i]);
            int x = half(DAT_806109c4 - w * 0x10);
            font_render_string(x, rowY, 0x10, prompt[i]);
            if (x < minX) minX = x;
            if (maxX < x + w * 0x10) maxX = x + w * 0x10;
            rowY += 0x10;
        }
        font_render_string(noCentreX - 0x3c, rowY + 0x20, 0x10, PTR_DAT_80412610);

        input_update_all();
        for (int pad = 0; pad < 4; ++pad) {
            if ((input_get_buttons_pressed(pad) & 1) != 0 ||
                input_get_analog_value(pad, 1) < (double)FLOAT_806166dc) {
                yesSelected = 1; startTick = os_get_time_base(); break;
            }
            if ((input_get_buttons_pressed(pad) & 2) != 0 ||
                (double)FLOAT_80616688 < input_get_analog_value(pad, 1)) {
                yesSelected = 0; startTick = os_get_time_base(); break;
            }
        }
        if (noCentreX - 0x3c < minX) minX = noCentreX - 0x3c;

        if (yesSelected) {
            gx_set_sub_material_id(2);
            gx_fill_rect_clipped(noCentreX - 0x3f, rowY + 0x1d,
                                 (noCentreX + noWidth * 0x10) - 0x37, rowY + 0x35, 0x7766e82f);
            gx_set_sub_material_id(1);
        }
        font_render_string(noCentreX + 0x3c, rowY + 0x20, 0x10, PTR_DAT_80412614);
        int w2 = FUN_8038b5c8((int)&PTR_DAT_80412614);
        if ((unsigned int)maxX < (unsigned int)(noCentreX + 0x3c + w2 * 0x10)) {
            maxX = noCentreX + 0x3c + w2 * 0x10;
        }
        if (!yesSelected) {
            gx_set_sub_material_id(2);
            gx_fill_rect_clipped(noCentreX + 0x39, rowY + 0x1d,
                                 noCentreX + 0x3c + w2 * 0x10 + 5, rowY + 0x35, 0x7766e82f);
            gx_set_sub_material_id(1);
        }

        gx_draw_line_i(minX - 0x1e, lineTopY - 0x1e, maxX + 0x1e, lineTopY - 0x1e, 0x7766e87f);
        gx_draw_line_i(minX - 0x1e, lineTopY - 0x1e, minX - 0x1e, lineBotY + 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, lineBotY + 0x1e, maxX + 0x1e, lineTopY - 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, lineBotY + 0x1e, minX - 0x1e, lineBotY + 0x1e, 0x7766e87f);

        int msg = FUN_803a4db4();
        if ((unsigned int)(msg - 4) < 3 || msg == 0xb || msg == -1) {
            boot_render_message_screen();
            startTick = os_get_time_base();
        }
        gx_frame_flush_wait(0, 0, 0, 0, 0, 0, 0, 0);
        vi_present_frame(1);
    }

    if (yesSelected) {
        gx_init_render_mode(FUN_8039ec70('\x01'), 0, 0, 0, 0, 0, 0, 0, 0);
    } else {
        FUN_8039ec70('\0');
    }

    if (DAT_80612398 != 0) *DAT_80612398 = 1;
    viewport_reset_default_bounds();
    screen_scale_update();
    particle_set_pool_sizes();
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    gx_reset_render_state();
    FUN_803c94d8();

    // 4 blank flush frames
    for (int i = 4; i >= 0; --i) {
        occ_query_flush_guard();
        gx_post_present_setup();
        input_update_all();
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);
        gx_frame_flush_wait(0, 0, 0, 0, 0, 0, 0, 0);
        vi_present_frame(1);
    }

    // ---- second screen: three-line follow-up ------------------------------
    minX = DAT_806109c4; maxX = 0;
    int line2TopY = (int)(-(float)half(DAT_806109c8) - FLOAT_806166e0);
    int line2BotY = (int)(-(float)half(DAT_806109c8) + FLOAT_806166f0);
    startTick = os_get_time_base();

    while (((unsigned int)(os_get_time_base() - startTick) / msDiv < 10000) &&
           (input_get_buttons_held(0) & 0x80) == 0 &&
           (input_get_buttons_held(1) & 0x80) == 0 &&
           (input_get_buttons_held(2) & 0x80) == 0 &&
           (input_get_buttons_held(3) & 0x80) == 0) {
        int textY = (int)(-(float)half(DAT_806109c8) - FLOAT_806166e0);
        occ_query_flush_guard();
        gx_post_present_setup();
        input_update_all();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);

        char **table = &PTR_s_Do_you_want_to_display_the_game_80412608;
        int rowY = textY;
        for (int i = 0; i < 3; ++i) {
            int idx = (i == 0) ? 4 : (i == 2) ? 5 : (7 - (yesSelected ? 0 : 1));
            int w = FUN_8038b5c8((int)table[idx]);
            int x = half(DAT_806109c4 - w * 0x10);
            font_render_string(x, rowY, 0x10, table[idx]);
            if (x < minX) minX = x;
            if (maxX < x + w * 0x10) maxX = x + w * 0x10;
            rowY += 0x10;
        }

        gx_draw_line_i(minX - 0x1e, line2TopY - 0x1e, maxX + 0x1e, line2TopY - 0x1e, 0x7766e87f);
        gx_draw_line_i(minX - 0x1e, line2TopY - 0x1e, minX - 0x1e, line2BotY + 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, line2BotY + 0x1e, maxX + 0x1e, line2TopY - 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, line2BotY + 0x1e, minX - 0x1e, line2BotY + 0x1e, 0x7766e87f);

        int msg = FUN_803a4db4();
        if ((unsigned int)(msg - 4) < 3 || msg == 0xb || msg == -1) {
            boot_render_message_screen();
            startTick = os_get_time_base();
        }
        gx_frame_flush_wait(0, 0, 0, 0, 0, 0, 0, 0);
        vi_present_frame(1);
    }

    viewport_reset_default_bounds();
    screen_scale_update();
    particle_set_pool_sizes();
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    mem_heap_reset_counters();
    gx_reset_render_state();

    FUN_80382704();
}

// halt_stub_assert (0x80181a70)
// 4-byte empty stub (blr) — an assert/halt placeholder that does nothing.
void halt_stub_assert(void)
{
}
