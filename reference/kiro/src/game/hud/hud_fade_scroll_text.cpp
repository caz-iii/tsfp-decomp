// Address: 0x80354980 | Name: hud_scroll_reset       | Date: 2026-09-12
// Address: 0x80354994 | Name: hud_scroll_advance     | Date: 2026-09-12
// Address: 0x80354af4 | Name: hud_scroll_draw        | Date: 2026-09-12
//
// HUD scrolling-text overlay driven by the match_hud_fade state (batch_1010).
// A vertical scrolling ticker (mission briefing / results credits) that walks a
// script table (DAT_8048283c, 0x1c-byte rows). Each row is a command:
//   type 4  = end-of-script
//   type 0  = vertical gap / spacer (advances the running Y by row +0x00)
//   type -1 = wrap marker (jump back to row 0)
//   other   = a text line to render (font id +0x00, string id short +0x10,
//             color +0x04, outline flag +0x08)
// The row layout, keyed off the &DAT_8048283c base, is:
//   +0x00 gap/font  +0x04 color  +0x08 outline-flag  +0x0c type
//   +0x10 string-id (short)  +0x14 gap-a (float)  +0x18 gap-b (float)
//
// hud_scroll_reset seeds the state; hud_scroll_advance is the per-frame tick
// (called from match_hud_fade_begin's sub-step); hud_scroll_draw renders the
// currently-visible window of the ticker.

#include "common.h"

// ---- per-file raw symbol externs (typed to match the call sites) ----------
extern int   DAT_80612dd0;      // current top-of-window row index
extern float FLOAT_80612dc4;    // running scroll offset (Y accumulator)
extern int   DAT_80612dcc;      // summed gap height above the window
extern float FLOAT_80612dc0;    // window bottom limit (row +0x14)
extern float FLOAT_80612dbc;    // window extra span   (row +0x18)
extern float FLOAT_80612dc8;    // vertical draw origin

extern int   DAT_80611748;      // viewport/render context base pointer
extern int   DAT_80611d54;      // active camera/player context
extern int   DAT_806109c0;      // screen height (pixels)
extern int   DAT_806109c4;      // screen width  (pixels)

// script table base (rows are 0x1c bytes; symbols alias the same table)
extern int   DAT_80482830;      // +0x00 gap height / font id
extern int   DAT_80482834;      // +0x04 color
extern int   DAT_80482838;      // +0x08 outline flag
extern int   DAT_8048283c;      // +0x0c command type
extern short DAT_80482840;      // +0x10 string id
extern float DAT_80482844;      // +0x14 window bottom
extern float DAT_80482848;      // +0x18 window span

// wave lookup table (2048 f32 entries) used for the ease curve
extern float DAT_80583228[];

// float/double constants
extern float  FLOAT_806108ec;
extern float  FLOAT_8061ad80;   // 0.0
extern float  FLOAT_8061ad84;   // 1.0 clamp
extern float  FLOAT_8061ad90;   // frame-scale denom
extern float  FLOAT_8061ad94;
extern float  FLOAT_8061ad98;
extern float  FLOAT_8061ad9c;
extern float  FLOAT_8061ada0;
extern float  FLOAT_8061ada4;
extern float  FLOAT_8061ada8;
extern float  FLOAT_8061adac;
extern float  FLOAT_8061adb0;
extern float  FLOAT_8061adb4;
extern float  FLOAT_8061adb8;
extern float  FLOAT_8061adbc;
extern float  FLOAT_8061adc0;   // fade alpha scale
extern double DOUBLE_8061ad88;  // 4503599627370496.0 (int->double bias)

// helpers (raw addresses; typed to the call sites)
extern void   FUN_803826e4(void);                 // begin text/render pass
extern void   FUN_80382730(void);                 // end text/render pass
extern double FUN_80390324(double x);              // ease/smooth curve
extern double expr_get_time_value(void);           // FUN_ alias: animation time
extern int    spawn_point_find_by_entity(int key);
extern void   camera_set_mode(int cam, int mode, int flag);
extern double player_get_camera_lerp_factor(int cam);
extern void   gx_set_sub_material_id(int id);
extern void   text_set_font_table(int fontId);
extern int    get_string_by_id(int id);
extern double text_get_char_width(unsigned char* s);
extern double text_get_char_height(unsigned char* s);
extern void   text_draw_at_int_coords(unsigned int* x, unsigned int* y,
                                       unsigned char* s, unsigned int color);
extern void   text_draw_char_outline(double x, double y, unsigned char* s,
                                      unsigned int color, int shadow);

// ============================================================================
// hud_scroll_reset  (0x80354980)
// Reset the ticker to the top of the script and zero the scroll offset.
// ============================================================================
void hud_scroll_reset(void)
{
    DAT_80612dd0 = 0;
    FLOAT_80612dc4 = FLOAT_8061ad80;
}

// ============================================================================
// hud_scroll_advance  (0x80354994)
// Per-frame tick: advance the scroll offset by the frame delta, and when the
// current top row's window height is exceeded, step to the next row (wrapping
// at a type -1 marker). Then recompute the summed gap height above the window
// and the vertical draw origin.
// ============================================================================
void hud_scroll_advance(void)
{
    int rowByte = DAT_80612dd0 * 0x1c;
    int nextRow = DAT_80612dd0;

    // Skip forward over the window until the end-of-script (type 4) marker,
    // counting rows so we know the candidate "next top" index.
    while (*(int*)(&DAT_8048283c + rowByte) != 4) {
        rowByte += 0x1c;
        nextRow += 1;
    }

    // Advance the scroll offset by dt (frame delta via the int->double idiom).
    FLOAT_80612dc4 = FLOAT_80612dc4 +
        FLOAT_806108ec /
        (float)((double)CONCAT44(0x43300000, (unsigned int)DAT_806109c0 ^ 0x80000000) - DOUBLE_8061ad88);

    // When the current top row scrolls fully off, step to the next row.
    {
        int topByte = DAT_80612dd0 * 0x1c;
        float threshold = FLOAT_8061ad90 + FLOAT_8061ad90 +
                          *(float*)(&DAT_80482844 + topByte) +
                          *(float*)(&DAT_80482848 + topByte);
        if (threshold < FLOAT_80612dc4) {
            DAT_80612dd0 = nextRow + 1;
            FLOAT_80612dc4 = FLOAT_8061ad80;
            if (*(int*)(&DAT_8048283c + DAT_80612dd0 * 0x1c) == -1) {
                DAT_80612dd0 = 0;
            }
        }
    }

    // Recompute the summed gap height for all type-0 rows above the terminator.
    DAT_80612dcc = 0;
    {
        int scanByte = DAT_80612dd0 * 0x1c;
        while (*(int*)(&DAT_8048283c + scanByte) != 4) {
            if (*(int*)(&DAT_8048283c + scanByte) == 0) {
                DAT_80612dcc += *(int*)(&DAT_80482830 + scanByte);
            }
            scanByte += 0x1c;
        }
        FLOAT_80612dc0 = *(float*)(&DAT_80482844 + scanByte);
        FLOAT_80612dbc = *(float*)(&DAT_80482848 + scanByte);
    }

    // Vertical draw origin = (screen_height/2 - gap_height/2) as float.
    {
        unsigned int h = *(unsigned int*)(DAT_80611748 + 0x20);
        int halfH = ((int)h >> 1) + (int)((int)h < 0 && (h & 1) != 0);
        int halfGap = ((int)DAT_80612dcc >> 1) +
                      (int)((int)DAT_80612dcc < 0 && (DAT_80612dcc & 1) != 0);
        FLOAT_80612dc8 = (float)((double)CONCAT44(0x43300000,
                          (unsigned int)(halfH - halfGap) ^ 0x80000000) - DOUBLE_8061ad88);
    }
}

// ============================================================================
// hud_scroll_draw  (0x80354af4)
// Render the currently-visible window of the ticker. Sets up an eased camera
// interpolation (used to pan a background as the credits roll), then walks the
// script rows drawing each text line at its computed X/Y with either a plain
// draw or an outlined draw. X is right/left/centre justified by the command
// sub-type (0/1 left, 2 centre, 3 right).
// ============================================================================
void hud_scroll_draw(void)
{
    FUN_803826e4();

    unsigned int justifiedX = 0;
    double fade = (double)FLOAT_8061ad80;

    if (FLOAT_80612dc0 <= FLOAT_80612dc4) {
        // --- eased camera pan from the animation timeline ---
        double t;
        t = expr_get_time_value();
        double a  = (double)((float)((double)FLOAT_8061ad94 * t) / FLOAT_8061ad90);
        double ea = (double)(float)FUN_80390324((double)((float)(a - (double)FLOAT_8061ad80) * FLOAT_8061ad98));
        t = expr_get_time_value();
        double b  = (double)((float)((double)FLOAT_8061ad9c * t) / FLOAT_8061ad90);
        double eb = (double)(float)FUN_80390324((double)((float)(b - (double)FLOAT_8061ad80) * FLOAT_8061ad98));
        t = expr_get_time_value();
        double c  = (double)((float)((double)FLOAT_8061ad94 * t) / FLOAT_8061ad90);
        double ec = (double)(float)FUN_80390324((double)((float)(c - (double)FLOAT_8061ad80) * FLOAT_8061ad9c));

        int ia = (int)-(float)((double)FLOAT_8061ada0 * ea - a);
        int ib = (int)-(float)((double)FLOAT_8061ada0 * eb - b);
        int sum = ia + ib;
        int shr = sum >> 0x1f;
        int spawnKey = ((sum * 8 | (unsigned int)(sum * 0x20000000 + shr) >> 0x1d) - shr) + 0x31
                       | 0x43524400;
        int camMode = spawn_point_find_by_entity(spawnKey);
        camera_set_mode(DAT_80611d54, camMode, 0);

        double lerp = player_get_camera_lerp_factor(DAT_80611d54);
        int ic = (int)-(float)((double)FLOAT_8061ada8 * ec - c);
        int shrc = ic >> 0x1f;
        double dc = (double)(CONCAT44(0x43300000,
                    (unsigned int)((ic * 4 | (unsigned int)(ic * 0x40000000 + shrc) >> 0x1e) - shrc)) ^ 0x80000000);
        float camY = (float)((double)FLOAT_8061ada4 +
                     (double)(((float)(dc - DOUBLE_8061ad88) *
                               (float)(lerp - (double)FLOAT_8061ada4)) / FLOAT_8061adac));
        *(float*)(DAT_80611d54 + 900)   = camY;
        *(float*)(DAT_80611d54 + 0x388) = camY;

        // --- eased fade level over the visible span ---
        float span  = FLOAT_8061ad90 + FLOAT_80612dbc;
        float range = FLOAT_80612dc4 - FLOAT_80612dc0;
        if (FLOAT_8061ad90 <= range) {
            if ((FLOAT_80612dbc <= FLOAT_8061ad80) || (span <= range)) {
                if (FLOAT_8061ad80 < FLOAT_8061ad90 + span) {
                    int idx = (int)(FLOAT_8061adb4 * FLOAT_8061adb8 * ((range - span) / FLOAT_8061ad90) + FLOAT_8061adb0);
                    fade = (double)(FLOAT_8061adb0 *
                           *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)) + FLOAT_8061adb0);
                }
            } else {
                fade = (double)FLOAT_8061ad84;
            }
        } else {
            unsigned int idx = (unsigned int)(FLOAT_8061adb4 *
                     (FLOAT_8061adb8 * (range / FLOAT_8061ad90) - FLOAT_8061adbc) + FLOAT_8061adb0);
            fade = (double)(FLOAT_8061adb0 * DAT_80583228[idx & 0x7ff] + FLOAT_8061adb0);
        }
        {
            double clamped = fade;
            if ((double)FLOAT_8061ad84 < fade) clamped = (double)FLOAT_8061ad84;
            if ((double)FLOAT_8061ad80 <= clamped) {
                if ((double)FLOAT_8061ad84 < fade) fade = (double)FLOAT_8061ad84;
            } else {
                fade = (double)FLOAT_8061ad80;
            }
        }

        gx_set_sub_material_id(1);
        double alphaScale = (double)FLOAT_8061adc0;

        unsigned int y = (unsigned int)FLOAT_80612dc8;
        int rowByte = DAT_80612dd0 * 0x1c;
        int shadow = (int)(alphaScale * fade);

        while (*(int*)(&DAT_8048283c + rowByte) != 4) {
            int cmd = *(int*)(&DAT_8048283c + rowByte);
            if (cmd == 0) {
                y += *(int*)(&DAT_80482830 + rowByte);
            } else {
                text_set_font_table(*(int*)(&DAT_80482830 + rowByte));
                unsigned char* str = (unsigned char*)get_string_by_id((int)*(short*)(&DAT_80482840 + rowByte));

                if (cmd == 2) {
                    // centre justify
                    double w = text_get_char_width(str);
                    int halfW = ((int)DAT_806109c4 >> 1) +
                                (int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0);
                    double hw = (double)(CONCAT44(0x43300000, (unsigned int)halfW) ^ 0x80000000);
                    justifiedX = (unsigned int)-(float)(w * (double)FLOAT_8061adb0 -
                                 (double)(float)(hw - DOUBLE_8061ad88));
                } else if (cmd < 2) {
                    if (0 < cmd) justifiedX = 0x40; // left inset
                } else if (cmd < 4) {
                    // right justify
                    double w = text_get_char_width(str);
                    double rw = (double)(CONCAT44(0x43300000, (unsigned int)(DAT_806109c4 - 0x40)) ^ 0x80000000);
                    justifiedX = (unsigned int)((double)(float)(rw - DOUBLE_8061ad88) - w);
                }

                text_get_char_height(str);

                if (*(int*)(&DAT_80482838 + rowByte) == 0) {
                    unsigned int xArg = justifiedX;
                    text_draw_at_int_coords(&xArg, &y, str, *(unsigned int*)(&DAT_80482834 + rowByte));
                } else {
                    double xf = (double)(CONCAT44(0x43300000, justifiedX) ^ 0x80000000);
                    double yf = (double)CONCAT44(0x43300000, y ^ 0x80000000);
                    text_draw_char_outline((double)(float)(xf - DOUBLE_8061ad88),
                                           (double)(float)(yf - DOUBLE_8061ad88), str,
                                           *(unsigned int*)(&DAT_80482834 + rowByte),
                                           (int)(alphaScale * fade));
                }
            }
            rowByte += 0x1c;
        }
        (void)shadow;
    }

    FUN_80382730();
    (void)justifiedX;
}
