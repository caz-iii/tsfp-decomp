// Batch 344 | Date: 2026-09-04
// HUD countdown timer: the large mm:ss countdown drawn during timed events. The
// per-view state lives in the DAT_80579dbc block (0x177c stride):
//   +0x00 remaining seconds (float), +0x04 last whole-second (int),
//   +0x08 expire callback (code*), +0x0c tick callback (code*).
//
//   0x80127bc0 hud_countdown_set     (init the timer + callbacks for a view)
//   0x80127c00 hud_countdown_render  (MASSIVE per-frame draw with pulse + expiry)
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_8061576c;
extern float  FLOAT_80615768;
extern float  FLOAT_80615784;
extern float  FLOAT_80615790;
extern float  FLOAT_80615794;
extern float  FLOAT_806157b8;
extern float  FLOAT_806157c0;
extern float  FLOAT_806157d4;
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615884;
extern double DOUBLE_80615738;  // uint->double bias

extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611748[];   // viewport rect
extern int    DAT_806109c0;     // frame delta accumulator (uint magnitude)
extern int    DAT_806109c4;     // render width
extern int    DAT_806108e4;     // frame delta (ticks)
extern float  DAT_80583228;     // sine LUT (indexed via &NAME)
extern int    DAT_80579dbc;     // countdown state block base (0x177c stride, via &NAME)
extern char   DAT_80610858[];   // "%02d" fmt (minus variant)
extern char   s__02d__02d_8040d1dc[]; // "%02d:%02d" fmt
extern char   DAT_8061087c[];   // ":" separator glyph template
extern char   DAT_80610878[];   // digit glyph metrics reference

// named helpers (raw)
extern void   FUN_803826e4(void);   // HUD push (countdown)
extern void   FUN_80382730(void);   // HUD pop  (countdown)
extern int    leaf_92c0(int mode);  // save/restore render state, returns token
extern int    text_set_font_table(int table);
extern int    text_set_color_rgb(double r, double g, double b);
extern double text_get_char_width(unsigned char* glyph);
extern void   text_draw_at_int_coords(uint* x, uint* y, unsigned char* glyph, uint color);
extern int    FUN_8016f00c(int mode);
extern void   FUN_80387fb8(double f1, double f2, double f3, double f4, double f5, double f6,
                           double f7, double f8, int outBuf, char* fmt, int a, int b,
                           int r7, int r8, int r9, int r10);   // sprintf-like glyph compose

// forward decls (this file)
extern "C" {
void hud_countdown_set(double seconds, int tickCb, int expireCb);
void hud_countdown_render(void);
}

// ============================================================================
// 0x80127bc0 | hud_countdown_set
// Initialize the active view's countdown: store the initial seconds (as float
// and truncated int), the tick callback (fired each whole-second change), and
// the expire callback (fired when it reaches 0).
// ============================================================================
extern "C" void hud_countdown_set(double seconds, int tickCb, int expireCb) {
    int off = *(int*)(DAT_80611d74 + 4) * 0x177c;
    *(float*)((int)&DAT_80579dbc + off) = (float)seconds;
    *(int*)((int)&DAT_80579dbc + off + 4) = (int)seconds;
    *(int*)((int)&DAT_80579dbc + off + 8) = tickCb;
    *(int*)((int)&DAT_80579dbc + off + 0xc) = expireCb;
    return;
}

// ============================================================================
// 0x80127c00 | hud_countdown_render  (MASSIVE)
// Per-frame countdown draw. If the remaining time has hit 0 it fires the expire
// callback (once) and clears it. Otherwise it decrements by the frame delta,
// fires the tick callback on each whole-second change, and draws the mm:ss text
// centered near the bottom with a per-second pulse (sine LUT) and a color ramp
// that intensifies in the final seconds. Called from hud_render_player_view.
// ============================================================================
extern "C" void hud_countdown_render(void) {
    unsigned char sepGlyph[4];
    uint drawX[3];
    unsigned char timeGlyph[8];
    unsigned char prevGlyph[8];

    FUN_803826e4();
    int off = *(int*)(DAT_80611d74 + 4) * 0x177c;
    float* remain = (float*)((int)&DAT_80579dbc + off);
    int* lastWhole = (int*)((int)&DAT_80579dbc + off + 4);
    int* tickCb = (int*)((int)&DAT_80579dbc + off + 8);
    int* expireCb = (int*)((int)&DAT_80579dbc + off + 0xc);

    if ((double)*remain <= (double)FLOAT_80615730) {
        if (*expireCb != 0) {
            ((void(*)(void))(*expireCb))();
            *expireCb = 0;
        }
        FUN_80382730();
        return;
    }

    uint whole = (uint)*remain;
    int token = leaf_92c0(1);
    double frac = (double)(float)((double)FLOAT_80615768 * (double)*remain);
    int phase = (int)((double)FLOAT_806157c0 * frac + (double)FLOAT_80615734);
    double pulse = (double)(FLOAT_80615790 *
        *(float*)((int)&DAT_80583228 + ((phase + 0x200) * 4 & 0x1ffc)) + FLOAT_806157b8);

    // fire tick callback on whole-second change
    if ((uint)*lastWhole != whole) {
        *lastWhole = (int)whole;
        if (*tickCb != 0) {
            ((void(*)(uint))(*tickCb))(whole);
        }
    }

    // decrement by frame delta, clamp at 0
    *remain = -((FLOAT_80615718 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738)) *
                (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - DOUBLE_80615738) - *remain);
    if ((double)*remain < (double)FLOAT_80615730) {
        *remain = FLOAT_80615730;
    }

    // subsecond fraction (for the final-second flash)
    double sub = (double)(float)((double)*remain - (double)(float)((double)CONCAT44(0x43300000, whole ^ 0x80000000) - DOUBLE_80615738));
    if (sub < (double)FLOAT_80615794) {
        double wf = (double)(float)((double)CONCAT44(0x43300000, whole ^ 0x80000000) - DOUBLE_80615738);
        if (wf <= (double)FLOAT_80615784) {
            double c = (double)(float)((double)FLOAT_80615884 *
                (double)(float)((double)FLOAT_80615718 -
                                (double)(float)((double)(float)((double)FLOAT_80615718 - sub) / (double)FLOAT_80615794)) +
                (double)FLOAT_80615718);
            text_set_color_rgb(c, c, (double)FLOAT_80615718);
        }
    }

    int secs = (int)whole % 0x3c;
    FUN_80387fb8(pulse, 0, 0, (double)FLOAT_80615734, 0, 0, 0, 0, (int)(intptr_t)timeGlyph,
                 s__02d__02d_8040d1dc, (int)whole / 0x3c, secs, 0, 0, 0, 0);
    text_set_font_table(0xc);
    double w = text_get_char_width(timeGlyph);
    double halfW = (double)FLOAT_80615734;
    uint cx = ((int)DAT_806109c4 >> 1) + (uint)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0);
    double flashLead = (double)(float)((double)FLOAT_80615718 - sub);
    int startIdx = 0;
    drawX[1] = 0x14;
    drawX[2] = (uint)-(float)(w * halfW - (double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738));

    // compose the "previous second" glyph for the crossfade (or a fixed sep)
    if ((double)FLOAT_80615794 < flashLead) {
        FUN_80387fb8(w, 0, DOUBLE_80615738, halfW, 0, 0, 0, 0, (int)(intptr_t)prevGlyph,
                     DAT_80610858, (int)(intptr_t)timeGlyph, secs, 0, 0, 0, 0);
    } else {
        FUN_80387fb8(w, 0, DOUBLE_80615738, halfW, 0, 0, 0, 0, (int)(intptr_t)prevGlyph,
                     s__02d__02d_8040d1dc, (int)(whole - 1) / 0x3c, (int)(whole - 1) % 0x3c, 0, 0, 0, 0);
    }

    // final-second zoom of the separator glyph
    if ((float)((double)CONCAT44(0x43300000, whole ^ 0x80000000) - DOUBLE_80615738) < FLOAT_806157d4) {
        double one = (double)FLOAT_80615718;
        double t = (double)((float)((double)CONCAT44(0x43300000, whole ^ 0x80000000) - DOUBLE_80615738) / FLOAT_806157d4);
        text_set_color_rgb(one, one, one);
        startIdx = 3;
        double sepW = text_get_char_width((unsigned char*)DAT_8061087c);
        double wD = (double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000);
        double zoom = (double)((float)((double)FLOAT_80615884 *
            (double)(float)(one - (double)(float)(flashLead / (double)FLOAT_80615794)) + one) *
            (float)((double)(float)(one - t) *
                    (double)(float)((double)((float)(wD - DOUBLE_80615738) * FLOAT_80615734) / sepW) + t));
        text_set_color_rgb(zoom, zoom, one);
        text_get_char_width((unsigned char*)DAT_8061087c);
        uint cx2 = ((int)DAT_806109c4 >> 1) + (uint)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0) ^ 0x80000000;
        (void)cx2;
        pulse = (double)(FLOAT_80615790 *
            *(float*)((int)&DAT_80583228 +
                      (((int)((double)FLOAT_806157c0 * (double)(float)((double)FLOAT_8061576c * frac) +
                              (double)FLOAT_80615734) + 0x200) * 4 & 0x1ffc)) + FLOAT_806157b8);
    }

    // draw each glyph column, crossfading digits that changed this frame
    unsigned char* prevP = prevGlyph + startIdx;
    unsigned char* curP = timeGlyph + startIdx;
    uint color = (int)((double)FLOAT_80615750 * pulse) & 0xff | 0x640a0a00;
    for (int i = startIdx; i < 5; i++) {
        sepGlyph[0] = *curP;
        sepGlyph[1] = 0;
        if (*prevP == sepGlyph[0]) {
            text_draw_at_int_coords(drawX + 2, drawX + 1, sepGlyph, color);
        } else {
            drawX[0] = drawX[2];
            text_draw_at_int_coords(drawX, drawX + 1, sepGlyph, color);
            int savedMode = FUN_8016f00c(2);
            uint fadeCol = (uint)((double)(FLOAT_80615750 *
                (FLOAT_80615718 - (float)(flashLead / (double)FLOAT_80615794))) * pulse);
            text_draw_at_int_coords(drawX + 2, drawX + 1, sepGlyph, fadeCol & 0xff | 0x32323200);
            FUN_8016f00c(savedMode);
        }
        prevP++;
        curP++;
    }

    double one = (double)FLOAT_80615718;
    text_set_color_rgb(one, one, one);
    leaf_92c0(token);
    FUN_80382730();
    return;
}
