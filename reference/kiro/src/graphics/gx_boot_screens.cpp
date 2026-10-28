// ============================================================================
// Address range: 0x8018008c - 0x80181c50 | batch_470 | Date: 2026-09-06
// Subsystem: graphics (bitmap-font text renderer + boot/present frame loops + FIFO mgmt).
// tag gx-boot.
//
// The boot / present front-end built on the gx_frame_present.cpp (batch_469) layer:
// a bitmap-font digit/text renderer, the per-frame present+advance driver (music/stream
// handoff), the "Licensed by Nintendo" boot screen, the 50/60Hz mode-select screen, an
// error/message screen, plus the command-FIFO resize (per region/disc) and GP-FIFO drain.
//
//   font_render_string        0x8018008c  bitmap-font string/digit renderer via glyph atlas
//   boot_ret_zero_a           0x80180434  stub -> 0
//   gx_present_and_advance    0x8018043c  present + music/stream advance + next-frame kick
//   boot_show_license_screen  0x8018064c  "Licensed by Nintendo" hold screen
//   font_draw_string_centered 0x801807ec  horizontally-centered font string
//   boot_freq_select_screen   0x80180850  50/60Hz PAL mode-select boot screen
//   boot_ret_zero_b           0x80181884  stub -> 0
//   boot_render_message_screen 0x8018188c error/status full-screen message
//   gx_fifo_resize_for_region 0x80181a74  (re)size the GX command FIFO by region/disc id
//   gx_gp_fifo_drain          0x80181c50  drain + reset the GP FIFO breakpoint state

#include "common.h"
#include "generated_decls.h"
#include "graphics.h"

// ---- typed aliases for helpers whose generated_decls proto is void / simplified ------
typedef unsigned int (*MemAllocAlignedFn)(unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          int, unsigned int, int, unsigned int,
                                          int, unsigned int, unsigned int, unsigned int);
#define FIFO_ALLOC ((MemAllocAlignedFn)mem_alloc_aligned)

// viewport_set_active takes a viewport record buffer pointer at the call sites.
typedef void (*ViewportSetActiveFn)(void *);
#define VIEWPORT_SET_ACTIVE ((ViewportSetActiveFn)viewport_set_active)

// ---- raw runtime helpers ----------------------------------------------------
extern unsigned long long FUN_803826c8(void);     // spawn-frame scratch (r3:r4 pair)
extern unsigned long long FUN_803826b4(void);      // spawn-frame scratch (pair)
extern unsigned long long FUN_803826f0(void);      // spawn-frame scratch (pair)
extern int         FUN_803826e4(void);             // spawn-frame scratch (single)
extern void        FUN_80382714(void);             // spawn-frame pop
extern void        FUN_8038273c(void);             // spawn-frame pop
extern void        FUN_80382730(void);             // spawn-frame pop
extern void        FUN_80382700(void);             // spawn-frame pop
extern int         FUN_8038b5c8(int strAddr);      // text pixel-width of string
extern void        FUN_803a5f0c(void *cb);         // register vblank/retrace callback
extern void        FUN_803a87b0(unsigned int mask);// AI/DSP mask
extern void        FUN_803a8d14(void *params);     // AI stream reconfigure
extern unsigned long long music_queue_track_full(unsigned long long, unsigned long long, unsigned long long,
                                                 unsigned long long, unsigned long long, unsigned long long,
                                                 unsigned long long, unsigned long long, int, int,
                                                 char *, unsigned int, unsigned int, void *, unsigned int, unsigned int);
extern unsigned int FUN_803ac020(void);            // stream pump idle
extern unsigned int FUN_803a5214(void);            // get retrace/AA state
extern void        FUN_803a78ac(int mode);         // set GP fifo owner
extern void        FUN_803a7710(void);             // resume/collect frame ptr
extern void        VIWaitForRetrace(void);
extern void        FUN_8039dcf4(unsigned long long, double, double, unsigned long long,
                                unsigned long long, unsigned long long, unsigned long long, unsigned long long,
                                unsigned int, int, int, unsigned int, unsigned int, void *, unsigned int, unsigned int);
extern int         boot_should_advance(void);
extern unsigned int boot_is_ready(void);
extern int         FUN_803a4db4(void);             // get video-mode/region code
extern void        FUN_8039edf0(int mode);         // set refresh mode 50/60
extern void        FUN_803994d0(unsigned int base, unsigned int size); // flush data cache
extern void        FUN_803c5630(int *fifo);
extern void        FUN_803c572c(int *fifo);
extern void        gx_setup_fifo_base(int *out, unsigned int base, unsigned int size);
extern void        FUN_803c5368(void *fifo);
extern void        FUN_803c5490(void *fifo);
extern void        FUN_801847f8(void);
extern void        FUN_803c6b7c(void);             // GX flush FIFO
extern void        FUN_803c5b64(void);             // GX finish/breakpoint sync
extern void        FUN_803c57c8(void *a, void *b, void *c, void *d, void *e); // read GP status bytes
extern int         FUN_801a30cc(void);             // UI overlay active?
extern int         FUN_801a2f0c(void);             // UI overlay present (used elsewhere)

// ---- globals ----------------------------------------------------------------
extern int   *DAT_80610a20;         // render-target pool [base, ?, freecursor, count]
extern int   DAT_80612254;          // active material record ptr
extern int   DAT_80612304;          // default material index
extern int   DAT_8061237c;          // draw-command depth
extern int   DAT_80612364;          // pending occlusion-query flag
extern int   DAT_806109c4;          // screen width
extern int   DAT_806109c8;          // screen height
extern int   DAT_806109c0;          // boot license-screen frame count
extern int   *DAT_80612398;         // boot-progress flag ptr
extern int   DAT_80611748;          // clip-rect array base (int-indexed)
extern char  DAT_80612380;          // audio-stream-disabled flag
extern int   DAT_8061239c;          // 2D-mode-active flag
extern int   DAT_800000f8;          // OS timer clock
extern int   DAT_806123a8;          // deferred error code (mode 0x66)
extern int   DAT_80610aa0;          // resolved error-screen table row
extern int   DAT_80612614;          // game-mode id
extern unsigned int *DAT_806123b4;  // active GX FIFO object
extern char  DAT_80501524;          // disc/region primary id
extern char  DAT_80501525;          // disc/region secondary id
extern char  DAT_805014d8;          // disc count
extern char  DAT_80611568;          // gp-drain gate flag
extern int   DAT_80612388;          // FIFO breakpoint write cursor
extern int   DAT_8061238c;          // FIFO breakpoint target cursor
extern int   DAT_80612390;          // last submitted breakpoint token
extern unsigned char DAT_80610a7a;  // GP fifo owner flag

extern float FLOAT_80616690;        // depth far
extern float FLOAT_8061668c;        // depth near base
extern float FLOAT_8060fad0;        // depth near bias
extern float FLOAT_806166d8;        // freq-screen line spacing a
extern float FLOAT_806166dc;        // freq-screen analog threshold lo
extern float FLOAT_80616688;        // freq-screen analog threshold hi
extern float FLOAT_806166e0;        // freq-screen line spacing b
extern float FLOAT_806166e4;        // freq-screen single-line offset
extern double DOUBLE_806166c0;      // int->float magic bias (0x43300000 pair)

// boot-screen string pointer tables (rodata)
extern void *PTR_s_This_game_supports_6O_Hz_mode__80412488;  // per-mode 11-entry * (5 lines + labels)
extern void *PTR_s_5O_Hz_mode_8041249c;
extern void *PTR_s_6O_Hz_mode_804124a0;
extern void *PTR_s_50_Hz_mode__804124a8;
extern char s_Licensed_by_Nintendo_804114c8[1];

// ---- forward decls (same-file cross calls) ----------------------------------
void font_render_string(int x0, int y0, unsigned int lineHeight, char *str);
void font_draw_string_centered(int y0, int lineHeight, char *str);
int  boot_ret_zero_b(void);
void gx_present_and_advance(unsigned long long p1, double p2, double p3, unsigned long long p4,
                            unsigned long long p5, unsigned long long p6, unsigned long long p7,
                            unsigned long long p8);
void boot_render_message_screen(void);

// font_render_string (0x8018008c)
// Render a bitmap-font string at (x0,y0) using the digit/glyph atlas (font_char_to_glyph
// -> 12x? sprite cells).  Handles '\n' newlines, several special glyph substitutions
// (0x2d/0x31/0x32/0x33), a superscript path (glyph >= 100), a split "H"-suffix time form,
// and a shadowed variant.  x0 arrives in the spawn-frame scratch (r3:r4 packed pair).
void font_render_string(int x0, int y0, unsigned int lineHeight, char *str)
{
    unsigned long long frame = FUN_803826c8();
    int startY = (int)(frame >> 0x20);   // packed y origin (from r3:r4)

    // bind default material or fall back
    if ((DAT_80612304 >= 0) && (DAT_80612304 < DAT_80610a20[3]) &&
        (DAT_80610a20[2] < DAT_80612304)) {
        DAT_80612254 = *DAT_80610a20 + DAT_80612304 * 0x58;
    } else {
        gx_bind_material_fallback();
    }
    gx_enter_2d_mode();

    int half = ((int)lineHeight >> 1) + (int)((int)lineHeight < 0 && (lineHeight & 1) != 0);
    int penX = x0;
    int penY = startY;
    char c;
    for (; (c = *str) != '\0'; str++) {
        if (c == '\n') {
            penX = penX + (int)lineHeight;
            penY = startY;
            continue;
        }
        unsigned int g = (unsigned int)font_char_to_glyph(c);
        int superscript = ((int)g >> 0x1f) + (int)(99 < g);
        int splitH = 0;
        int dotForm = 0;
        if (superscript != 0) g = g - 100;

        if (g == 0x2d)      { g = 0x25; splitH = 1; }
        else if (g == 0x31) { g = 0xe;  dotForm = 1; }
        else if (g == 0x32) { g = 0x14; dotForm = 1; }
        else if (g == 0x33) { g = 8;    dotForm = 1; }

        if (g != 0xffffffff) {
            int col = (int)g % 10;
            int row = (int)g / 10;
            int u = col * 0xc;
            int v = row * 0xc;
            if (superscript == 0) {
                if (splitH) {
                    gx_draw_sprite_rect(penX, penY + 3, penX + lineHeight, penY + half + 3,
                                        col * 0xc0, (v + 6) * 0x10, (u + 0xc) * 0x10, (v + 0xc) * 0x10);
                } else if (dotForm) {
                    unsigned int right = penX + lineHeight;
                    unsigned int midY = penY + (((int)lineHeight >> 2) +
                                               (int)((int)lineHeight < 0 && (lineHeight & 3) != 0));
                    gx_draw_sprite_rect(penX, penY, right, midY, 0xc0, 0x300, 0x180, 0x330);
                    if (g == 8) {
                        gx_draw_sprite_rect(penX, midY, right, penY + lineHeight, 0x600, 0x30, 0x6c0, 0xc0);
                    } else {
                        gx_draw_sprite_rect(penX, midY, right, penY + lineHeight, col * 0xc0, row * 0xc0,
                                            (u + 0xc) * 0x10, (v + 0xc) * 0x10);
                    }
                } else if (*str == '-') {
                    int gH = font_char_to_glyph('H');
                    int baseY = penY + half;
                    int uH = (gH % 10) * 0xc;
                    unsigned int vH = ((gH / 10) * 0xc + 8) * 0x10;
                    gx_draw_sprite_rect(penX + 2, baseY, penX + (lineHeight - 2), baseY + 4,
                                        (uH + 3) * 0x10, vH, (uH + 4) * 0x10, vH);
                } else {
                    gx_draw_sprite_rect(penX, penY, penX + lineHeight, penY + lineHeight,
                                        col * 0xc0, row * 0xc0, (u + 0xc) * 0x10, (v + 0xc) * 0x10);
                }
            } else {
                // superscript / shadowed pair
                gx_draw_sprite_rect(penX, penY + 3, penX + lineHeight, penY + lineHeight,
                                    col * 0xc0, row * 0xc0, (u + 0xc) * 0x10, (v + 0xc) * 0x10);
                gx_draw_sprite_rect(penX, penY + 3, penX + lineHeight, penY + 5, 0x540, 0x80, 0x600, 0xa0);
            }
        }
        penX = penX + lineHeight;
    }
    (void)y0; (void)startY;
    FUN_80382714();
}

// boot_ret_zero_a (0x80180434) - stub returning 0.
int boot_ret_zero_a(void) { return 0; }

// gx_present_and_advance (0x8018043c)
// Present the current frame then advance the audio/music/stream systems and kick the
// next frame.  On the shutdown/fade path (param_1==0) it runs a fixed clear-and-present
// loop; otherwise it does a single flush.  Called from gx_frame_flush_wait's next-frame
// path and the boot present loops.
void gx_present_and_advance(unsigned long long p1, double p2, double p3, unsigned long long p4,
                            unsigned long long p5, unsigned long long p6, unsigned long long p7,
                            unsigned long long p8)
{
    unsigned char aiCfg[96];
    unsigned int cfg0, cfg1;
    int mode = (int)FUN_803826f0();

    cfg0 = /* DAT_803ccfd8 */ 0; cfg1 = 0;
    // (the 4-word AI stream config is copied from DAT_803ccfd8.. into a local; we snapshot
    //  it into aiCfg for FUN_803a8d14 below.)
    DAT_8061237c = 1;
    FUN_803a5f0c((void *)occ_query_flush);
    if (mode != 0) {
        gx_frame_flush_wait(0, p2, p3, p4, p5, p6, p7, p8);
    }
    FUN_803a87b0(0xf0000000);
    FUN_803a8d14(aiCfg);
    sound_reset_streams();
    music_queue_track_full(0, p2, p3, p4, p5, p6, p7, p8, 0, 0,
                           (char *)1, cfg1, cfg0, (void *)0, 0, 0);
    if (DAT_80612380 == '\0') {
        FUN_803ac020();
    }

    if (mode == 0) {
        unsigned int aa = FUN_803a5214();
        if (DAT_8061239c == 0) {
            VIEWPORT_SET_ACTIVE(aiCfg);
            viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
            double far = (double)FLOAT_80616690;
            double near = -(double)(FLOAT_8061668c + FLOAT_8060fad0);
            viewport_set_depth_params_active((double)(FLOAT_8061668c + FLOAT_8060fad0), near, far);
            viewport_set_gx_active();
            gx_set_scissor_rect((&DAT_80611748)[0], (&DAT_80611748)[5], (&DAT_80611748)[1], (&DAT_80611748)[6]);
            vi_present_frame(1);
            int i = 0x1d;
            do {
                occ_query_flush_guard();
                gx_post_present_setup();
                gx_set_sub_material_id(1);
                gx_set_blend_mode(0);
                gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);
                gx_bind_material_fallback();
                gx_draw_sprite_rect(0, 0, DAT_806109c4, DAT_806109c8, 0xa0, 0xa0,
                                    (DAT_806109c4 - 10) * 0x10, (DAT_806109c8 - 10) * 0x10);
                gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
                vi_present_frame(1);
                i = i - 1;
            } while (0 < i + 1);
        } else {
            gx_frame_flush_wait(0, p2, p3, p4, p5, p6, p7, p8);
        }
        FUN_803a78ac(1);
        FUN_803a7710();
        VIWaitForRetrace();
        unsigned int clz = countLeadingZeros(aa);
        FUN_8039dcf4(0, p2, p3, p4, p5, p6, p7, p8, clz >> 5, 1, 0, cfg1, cfg0, (void *)0, 0, 0);
    } else {
        FUN_803a78ac(1);
        FUN_803a7710();
        VIWaitForRetrace();
        FUN_8039dcf4(0, p2, p3, p4, p5, p6, p7, p8, 1, 1, 1, cfg1, cfg0, (void *)0, 0, 0);
    }
    FUN_8038273c();
}

// boot_show_license_screen (0x8018064c)
// Initialise the render-target heap and hold on the "Licensed by Nintendo" screen for
// DAT_806109c0 frames, then present 4 blank frames and (on certain regions) spin out
// the rest of a timer.  Called from game_entry_point.
void boot_show_license_screen(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                              unsigned long long p4, unsigned long long p5, unsigned long long p6,
                              unsigned long long p7, unsigned long long p8)
{
    unsigned char vp[104];
    FUN_803826e4();
    int frames = DAT_806109c0;
    int textW = FUN_8038b5c8(-0x7fbeeb38);  // width of "Licensed by Nintendo"

    mem_heap_init();
    render_target_pool_alloc();
    VIEWPORT_SET_ACTIVE(vp);
    viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
    double far = (double)FLOAT_80616690;
    double near = -(double)(FLOAT_8061668c + FLOAT_8060fad0);
    viewport_set_depth_params_active((double)(FLOAT_8061668c + FLOAT_8060fad0), near, far);
    viewport_set_gx_active();
    gx_set_scissor_rect((&DAT_80611748)[0], (&DAT_80611748)[5], (&DAT_80611748)[1], (&DAT_80611748)[6]);
    int startTime = os_get_time_base();

    while (frames != 0) {
        frames = frames - 1;
        int cx = ((int)DAT_806109c4 >> 1) +
                 (int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0);
        int y = DAT_806109c8 - 100;
        occ_query_flush_guard();
        gx_post_present_setup();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);
        font_render_string((cx) - ((textW << 4) >> 1), y, 0x10, s_Licensed_by_Nintendo_804114c8);
        gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
        vi_present_frame(1);
    }
    int i = 4;
    do {
        occ_query_flush_guard();
        gx_post_present_setup();
        gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
        vi_present_frame(1);
        i = i - 1;
    } while (i != 0);

    int region = FUN_803a4db4();
    if (((region - 4U) < 3) || (region == 0xb) || (region == -1)) {
        unsigned int elapsed = 0;
        while (elapsed < (unsigned int)(DAT_800000f8 >> 2)) {
            elapsed = os_get_time_base() - startTime;
        }
    }
    mem_heap_reset_counters();
    gx_reset_render_state();
    FUN_80382730();
}

// font_draw_string_centered (0x801807ec)
// Draw a font string horizontally centred on screen at the given base position.
void font_draw_string_centered(int y0, int lineHeight, char *str)
{
    unsigned long long frame = FUN_803826f0();
    int w = FUN_8038b5c8((int)str);
    unsigned int free = DAT_806109c4 - w * (unsigned int)frame;
    font_render_string(((int)free >> 1) + (int)((int)free < 0 && (free & 1) != 0),
                       (int)(frame >> 0x20), (unsigned int)frame, str);
    (void)y0; (void)lineHeight;
    FUN_8038273c();
}

// boot_freq_select_screen (0x80180850)
// The PAL 50/60Hz refresh-rate select boot screen: renders the localized info lines and
// two buttons (50 Hz / 60 Hz), reads controller input for up to 30000 ms to choose,
// highlights the selection, then applies the chosen refresh mode (FUN_8039edf0) and
// re-inits the render mode + viewport before returning to the boot flow.
void boot_freq_select_screen(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                             unsigned long long p4, unsigned long long p5, unsigned long long p6,
                             unsigned long long p7, unsigned long long p8)
{
    unsigned char vp[72];
    FUN_803826b4();
    if ((boot_should_advance() == 0) || (*DAT_80612398 != 0)) {
        FUN_80382700();
        return;
    }

    int langRow = boot_ret_zero_b();                 // FUN_80181884 language/table row (0)
    unsigned int sel = boot_is_ready();
    sel = countLeadingZeros(sel) >> 5;               // initial highlighted button

    int cx = DAT_806109c4;
    int cyHalf = ((int)DAT_806109c8 >> 1) +
                 (int)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0);
    unsigned int minX = DAT_806109c4;
    unsigned int maxX = 0;
    double spacingA = (double)FLOAT_806166d8;
    // frame box top/bottom (int->double magic-bias dequant of cyHalf +/- spacing)
    int boxTop = (int)((double)(float)((double)(CONCAT44(0x43300000, cyHalf) ^ 0x80000000) - DOUBLE_806166c0) - spacingA);
    int boxBot = (int)((double)(float)((double)(CONCAT44(0x43300000, cyHalf) ^ 0x80000000) - DOUBLE_806166c0) + spacingA);

    mem_heap_init();
    render_target_pool_alloc();
    VIEWPORT_SET_ACTIVE(vp);
    viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
    double far = (double)FLOAT_80616690;
    double near = -(double)(FLOAT_8061668c + FLOAT_8060fad0);
    viewport_set_depth_params_active((double)(FLOAT_8061668c + FLOAT_8060fad0), near, far);
    viewport_set_gx_active();
    gx_set_scissor_rect((&DAT_80611748)[0], (&DAT_80611748)[5], (&DAT_80611748)[1], (&DAT_80611748)[6]);
    int startTime = os_get_time_base();
    input_update_all();

    double thrLo = (double)FLOAT_806166dc;
    double thrHi = (double)FLOAT_80616688;
    void **labelTbl = (void **)(&PTR_s_6O_Hz_mode_804124a0) + langRow * 0xb;

    // --- selection loop (timeout 30000ms or Start pressed) ---
    while (((unsigned int)(os_get_time_base() - startTime) / (DAT_800000f8 / 4000) < 30000) &&
           ((input_get_buttons_pressed(0) & 0x80) == 0) &&
           ((input_get_buttons_pressed(1) & 0x80) == 0) &&
           ((input_get_buttons_pressed(2) & 0x80) == 0) &&
           ((input_get_buttons_pressed(3) & 0x80) == 0)) {
        int lineY = (int)((double)(float)((double)(CONCAT44(0x43300000,
                     ((int)DAT_806109c8 >> 1) + (int)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0)) ^ 0x80000000)
                     - DOUBLE_806166c0) - spacingA);
        int label50W = FUN_8038b5c8((int)(&PTR_s_5O_Hz_mode_8041249c)[langRow * 0xb]);
        unsigned int uw = DAT_806109c4 + label50W * -0x10;
        int centerX = ((int)uw >> 1) + (int)((int)uw < 0 && (uw & 1) != 0);

        occ_query_flush_guard();
        gx_post_present_setup();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);

        // 5 info lines
        int prevY = lineY;
        for (int line = 0, off = 0; line < 5; line++, off += 4) {
            int lw = FUN_8038b5c8(*(int *)((int)&PTR_s_This_game_supports_6O_Hz_mode__80412488 + off + langRow * 0x2c));
            unsigned int lx = DAT_806109c4 + lw * -0x10;
            lx = ((int)lx >> 1) + (int)((int)lx < 0 && (lx & 1) != 0);
            font_render_string((int)lx, prevY, 0x10,
                               *(char **)((int)&PTR_s_This_game_supports_6O_Hz_mode__80412488 + off + langRow * 0x2c));
            if ((int)lx < (int)minX) minX = lx;
            unsigned int endX = lx + lw * 0x10;
            if ((int)maxX < (int)endX) maxX = endX;
            prevY = prevY + 0x10;
        }
        int btnY = prevY;
        font_render_string(centerX - 0x78, btnY + 0x20, 0x10, (char *)(&PTR_s_5O_Hz_mode_8041249c)[langRow * 0xb]);

        // poll for a fresh press / analog nudge
        input_update_all();
        for (int pad = 0; pad < 4; pad++) {
            if (((input_get_buttons_pressed(pad) & 1) != 0) ||
                (input_get_analog_value(pad, 1) < thrLo)) {
                sel = 1; startTime = os_get_time_base(); break;
            }
            if (((input_get_buttons_pressed(pad) & 2) != 0) ||
                (thrHi < input_get_analog_value(pad, 1))) {
                sel = 0; startTime = os_get_time_base(); break;
            }
        }
        if ((int)(centerX - 0x78) < (int)minX) minX = centerX - 0x78;

        if (sel != 0) {
            gx_set_sub_material_id(2);
            gx_fill_rect_clipped(centerX - 0x7b, btnY + 0x1d, (centerX + label50W * 0x10) - 0x73, btnY + 0x35, 0x7766e82f);
            gx_set_sub_material_id(1);
        }
        font_render_string(centerX + 0x78, btnY + 0x20, 0x10, (char *)*labelTbl);
        int lblW = FUN_8038b5c8((int)*labelTbl);
        int rx = centerX + 0x78;
        if (maxX < (unsigned int)(rx + lblW * 0x10)) maxX = rx + lblW * 0x10;
        if (sel == 0) {
            gx_set_sub_material_id(2);
            gx_fill_rect_clipped(centerX + 0x75, btnY + 0x1d, rx + lblW * 0x10 + 5, btnY + 0x35, 0x7766e82f);
            gx_set_sub_material_id(1);
        }
        // frame box
        gx_draw_line_i(minX - 0x1e, boxTop - 0x1e, maxX + 0x1e, boxTop - 0x1e, 0x7766e87f);
        gx_draw_line_i(minX - 0x1e, boxTop - 0x1e, minX - 0x1e, boxBot + 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, boxBot + 0x1e, maxX + 0x1e, boxTop - 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX + 0x1e, boxBot + 0x1e, minX - 0x1e, boxBot + 0x1e, 0x7766e87f);
        int region = FUN_803a4db4();
        if (((region - 4U) < 3) || (region == 0xb) || (region == -1)) {
            boot_render_message_screen();
            startTime = os_get_time_base();
        }
        gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
        vi_present_frame(1);
    }

    // --- confirm splash (2000ms) ---
    *DAT_80612398 = 1;
    double spacingB = (double)FLOAT_806166e0;
    int confTime = os_get_time_base();
    while ((unsigned int)(os_get_time_base() - confTime) / (DAT_800000f8 / 4000) < 2000) {
        int lineCount = 2;
        int lineY = (int)((double)(float)((double)(CONCAT44(0x43300000,
                     ((int)DAT_806109c8 >> 1) + (int)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0)) ^ 0x80000000)
                     - DOUBLE_806166c0) - spacingB);
        occ_query_flush_guard();
        gx_post_present_setup();
        gx_set_sub_material_id(1);
        gx_set_blend_mode(0);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);
        if ((&PTR_s_50_Hz_mode__804124a8)[langRow * 0xb] == (void *)0) {
            lineCount = 1;
            lineY = (int)((float)((double)(CONCAT44(0x43300000,
                     ((int)DAT_806109c8 >> 1) + (int)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0)) ^ 0x80000000)
                     - DOUBLE_806166c0) - FLOAT_806166e4);
        }
        int minX2 = DAT_806109c4, maxX2 = 0;
        int yy = lineY;
        for (int line = 7; line < lineCount + 7; line++) {
            int idx = langRow * 0xb + line + (~-(int)(sel != 0) & 2);
            int lw = FUN_8038b5c8((int)(&PTR_s_This_game_supports_6O_Hz_mode__80412488)[idx]);
            unsigned int lx = DAT_806109c4 + lw * -0x10;
            lx = ((int)lx >> 1) + (int)((int)lx < 0 && (lx & 1) != 0);
            font_render_string((int)lx, yy, 0x10, (char *)(&PTR_s_This_game_supports_6O_Hz_mode__80412488)[idx]);
            if ((int)lx < minX2) minX2 = lx;
            int endX = lx + lw * 0x10;
            if (maxX2 < endX) maxX2 = endX;
            yy = yy + 0x10;
        }
        gx_draw_line_i(minX2 - 0x1e, boxTop - 0x1e, maxX2 + 0x1e, boxTop - 0x1e, 0x7766e87f);
        gx_draw_line_i(minX2 - 0x1e, boxTop - 0x1e, minX2 - 0x1e, boxBot + 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX2 + 0x1e, boxBot + 0x1e, maxX2 + 0x1e, boxTop - 0x1e, 0x7766e87f);
        gx_draw_line_i(maxX2 + 0x1e, boxBot + 0x1e, minX2 - 0x1e, boxBot + 0x1e, 0x7766e87f);
        int region = FUN_803a4db4();
        if (((region - 4U) < 3) || (region == 0xb) || (region == -1)) {
            boot_render_message_screen();
            confTime = os_get_time_base();
        }
        gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
        vi_present_frame(1);
    }

    if (sel == 0) {
        gx_init_render_mode(0, near, far, p4, p5, p6, p7, p8);
        FUN_8039edf0(1);
    } else {
        FUN_8039edf0(0);
    }
    viewport_reset_default_bounds();
    screen_scale_update();
    particle_set_pool_sizes();
    gx_set_scissor_rect((&DAT_80611748)[0], (&DAT_80611748)[5], (&DAT_80611748)[1], (&DAT_80611748)[6]);
    int fi = 4;
    do {
        occ_query_flush_guard();
        gx_post_present_setup();
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0x7f);
        gx_frame_flush_wait(0, near, far, p4, p5, p6, p7, p8);
        vi_present_frame(1);
        fi = fi - 1;
    } while (fi + 1 > 0);
    mem_heap_reset_counters();
    gx_reset_render_state();
    (void)cx;
    FUN_80382700();
}

// boot_ret_zero_b (0x80181884) - stub returning 0 (language/table row selector).
int boot_ret_zero_b(void) { return 0; }

// boot_render_message_screen (0x8018188c)
// Render a full-screen status/error message from a per-code string table when the video
// mode / disc-error code (FUN_803a4db4, or the deferred DAT_806123a8 in mode 0x66) maps
// to a known row.  Rows: 4/6->1, 5->0, 0xb->3, -1->4.  Draws each line centred and
// presents when a frame is in flight.
void boot_render_message_screen(void)
{
    unsigned char vp[104];
    int frameInFlight = FUN_803826e4();
    int code = FUN_803a4db4();
    int row = -1;
    int y = DAT_806109c8 - 100;

    if ((code == 0) && (DAT_80612614 == 0x66)) {
        code = DAT_806123a8;
    }
    switch (code) {
    case 4:  row = 1; break;
    case 5:  row = 0; break;
    case 6:  row = 1; break;
    case 0xb: row = 3; break;
    case -1: row = 4; break;
    }
    DAT_80610aa0 = row;

    if ((row != -1) && (DAT_8061237c == 0) && (FUN_801a30cc() == 0)) {
        int langRow = boot_ret_zero_b();
        int **strList = (int **)(row * 100 + -0x7fbee1b0 + langRow * 0x14);
        int savedMat = gx_set_sub_material_id(1);
        int savedBlend = gx_set_blend_mode(0);
        int savedClip = DAT_80611748;
        FUN_803a5f0c((void *)occ_query_flush);
        VIWaitForRetrace();
        if (frameInFlight != 0) {
            occ_query_flush_guard();
            gx_post_present_setup();
        }
        VIEWPORT_SET_ACTIVE(vp);
        viewport_set_rect_active(0, 0, DAT_806109c4, DAT_806109c8);
        double far = (double)FLOAT_80616690;
        double near = -(double)(FLOAT_8061668c + FLOAT_8060fad0);
        viewport_set_depth_params_active((double)(FLOAT_8061668c + FLOAT_8060fad0), near, far);
        viewport_set_gx_active();
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0xff);
        for (; *strList != (int *)0; strList++) {
            font_draw_string_centered(y, 0xc, (char *)*strList);
            y = y + 0xc;
        }
        gx_set_sub_material_id(savedMat);
        gx_set_blend_mode(savedBlend);
        DAT_80611748 = savedClip;
        if (savedClip != 0) {
            viewport_set_gx_active();
        }
        if (frameInFlight != 0) {
            gx_frame_flush_wait(0, near, far, 0, 0, 0, 0, 0);
            vi_present_frame(1);
        }
    }
    FUN_80382730();
}

// gx_fifo_resize_for_region (0x80181a74)
// (Re)allocate the GX command FIFO sized for the current disc/region (DAT_80501524/25 +
// disc count DAT_805014d8): defaults 0xc0000, 0x8e000 for region 'f', up to 0x180000 /
// 0xf2000 / per-disc scaled sizes for others.  Rebuilds the FIFO object and re-arms the
// occlusion query.  Called from outer_game_loop.
void gx_fifo_resize_for_region(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                               unsigned long long p4, unsigned long long p5, unsigned long long p6,
                               unsigned long long p7, unsigned long long p8)
{
    unsigned int size = 0xc0000;
    if (DAT_80501524 == 'f') {
        size = 0x8e000;
    } else {
        unsigned int sec = (unsigned int)DAT_80501525;
        if ((sec == 10) &&
            (((DAT_805014d8 > '\x01') && ((DAT_80501524 == '\x02') || (DAT_80501524 == '\x1a'))) ||
             ((DAT_80501524 == '8') || (DAT_80501524 == 'Q') || (DAT_80501524 == '\x06')))) {
            size = 0x180000;
        } else if ((sec == 10) && (DAT_805014d8 > '\x01') && (DAT_80501524 == '\v')) {
            size = 0xf2000;
        } else if (DAT_80501524 == 'd') {
            int discs = (int)DAT_805014d8;
            size = (discs < 3) ? (unsigned int)(discs * 0x19c800) : (unsigned int)(discs * 0x16a800);
        } else if ((DAT_80501524 < 'd') && (sec != 10) && (sec != 0)) {
            size = 0x180000;
        } else if (((DAT_80501524 == '\x0e') || (DAT_80501524 == '\x01') || (DAT_80501524 == '\x16')) &&
                   (DAT_805014d8 > '\x01')) {
            size = 0x13d000;
        }
    }
    occ_read_mode_set(1);
    unsigned int base = FIFO_ALLOC(p1, p2, p3, p4, p5, p6, p7, p8, 0x20, size, 1, 0, 0, 0, 0, 0);
    FUN_803994d0(base, size);
    FUN_803c5630((int *)DAT_806123b4);
    FUN_803c572c((int *)DAT_806123b4);
    gx_setup_fifo_base((int *)DAT_806123b4, base, size);
    FUN_803c5368(DAT_806123b4);
    FUN_803c5490(DAT_806123b4);
    gx_draw_done_tokens();
    DAT_80612364 = 1;
    DAT_8061237c = 0;
    occ_scratch_reset();
    FUN_801847f8();
}

// gx_gp_fifo_drain (0x80181c50)
// Drain and reset the GP FIFO: flush (FUN_803c6b7c), reset the breakpoint cursors/token,
// finish (FUN_803c5b64), then spin reading the GP status bytes until the pipe reports
// idle (busy && read-ok && !overflow).  Gated on DAT_80611568.  From inner_game_loop.
void gx_gp_fifo_drain(void)
{
    unsigned char st[5];
    unsigned char b_read = 0, b_rdok = 0, b_ovf = 0;

    if (DAT_80611568 == 0) {
        FUN_803c6b7c();
        DAT_80612388 = 0;
        DAT_8061238c = 0;
        DAT_80612390 = 0;
        DAT_80610a7a = 1;
        DAT_80612364 = 1;
        FUN_803c5b64();
        FUN_803c6b7c();
        while ((b_read == 0) || (b_rdok == 0) || (b_ovf != 0)) {
            FUN_803c57c8(&st[0], &st[1], &b_read, &b_rdok, &b_ovf);
        }
    }
}
