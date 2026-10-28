// Batch 344 | Date: 2026-09-04
// HUD off-screen / on-screen target box marker. Draws the bracketed targeting box
// around a world point or entity (used for radar dots and objective markers): the
// four corner-bracket sprites, an optional expanding/rotating frame with side
// panels (FUN_80170dbc), and the distance + bearing text labels. Alpha follows
// the marker fade (param_2) and screen coverage; returns 0 if the target was
// culled (fully off-screen / behind camera), non-zero if drawn.
//
//   0x801259ac hud_target_box_render
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signature + faithful body. Because the original
// is a very large screen-projection routine dominated by int<->float bias
// conversions, the body is expressed as the same computation using plain float
// math (the CONCAT44/DOUBLE bias idiom is a decompiler artifact of int->float
// casts); behaviour and draw-call order are preserved.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_80615754;
extern float  FLOAT_8061572c;
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_80615768;
extern float  FLOAT_80615770;
extern float  FLOAT_80615784;
extern float  FLOAT_806157b4;
extern float  FLOAT_806157f4;
extern float  FLOAT_80615848;
extern float  FLOAT_806159a8;
extern float  FLOAT_806159ac;
extern double DOUBLE_80615738;  // uint->double bias
extern double DOUBLE_80615740;  // int->double bias

extern int    DAT_80611d54;     // active render/camera context
extern int    DAT_80611748[];   // viewport rect
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_806109c4;     // render width
extern int    DAT_806109c8;     // render height
extern int    DAT_80611928;     // frame counter
extern int    DAT_806108e4;     // frame delta ticks
extern char   DAT_806108a8[];   // distance-text glyph template
extern char   DAT_806108b0[];   // bearing-text glyph template
extern char   DAT_806108b8[];   // coordinate-digit glyph template
extern char   DAT_80610878[];   // digit metrics reference

// named helpers (raw)
extern unsigned long long FUN_803826bc(void);   // HUD push (target box), returns (entity<<32 | ...)
extern void   FUN_80382708(void);                // HUD pop  (target box)
extern void   FUN_8016e77c(int texId);
extern void   FUN_80170410(int x0, int y0, int x1, int y1, int u, int v, uint tw, uint th);
extern void   FUN_80170dbc(double a, double b, double c, double d, double e, double f, double g,
                           double h, int i, int j, int k, uint tw, uint th, int l, uint m, uint n,
                           float o, uint c1, uint c2, uint c3, uint c4);   // gradient quad
extern int    sprite_get_texture_id(int spriteId);
extern int    entity_compute_screen_bounds(int ent, uint* x0, int* y0, int* x1, int* y1, float* z, int flag);
extern void   FUN_8012fad0(float* mtx, float* pt, float* out);   // project point
extern double FUN_80131538(double x);            // sqrt
extern int    leaf_92c0(int mode);               // save/restore render state token
extern int    text_set_font_table(int table);
extern int    text_set_color_rgb(double r, double g, double b);
extern double text_get_char_width(unsigned char* glyph);
extern double text_get_char_height(unsigned char* glyph);
extern void   text_draw_char(double x, double y, unsigned char* glyph, uint color);
extern double FUN_80390aa4(double y, double x);  // atan2
extern unsigned char* FUN_8012e878(double, double, double, double, double, double, double, double,
                                   void* fmt, int a, int b, int c, int d, int e, int f, int g);
extern int    FUN_8013104c(void);                // random coordinate digit

// forward decl (this file)
extern "C" int hud_target_box_render(double p1, double p2, float* worldPt, int ent, int sprite,
                                     uint baseColor, int expanding);

// ============================================================================
// 0x801259ac | hud_target_box_render
// Projects the target (an entity when ent!=0, otherwise the world point worldPt)
// to screen-space bounds. If visible: draws the four corner brackets (sprite
// 'sprite'), and when 'expanding' also an animated frame with gradient side
// panels and a distance/bearing/coordinate text readout. Alpha scales with
// param_2 (fade) and the on-screen box area. Returns 1 when drawn, 0 when culled.
// ============================================================================
extern "C" int hud_target_box_render(double /*p1*/, double p2, float* worldPt, int ent, int sprite,
                                     uint baseColor, int expanding) {
    uint x0, y0, x1, y1;    // local_2e4/2ec/2e8/2f0 screen bounds
    float proj[4];          // local_2e0[..] projected point (x,y,z,w-ish)

    unsigned long long push = FUN_803826bc();
    float* pt = (float*)(uintptr_t)(push >> 0x20);   // resolved camera-space point ptr
    int entArg = (int)push;
    double dist = (double)FLOAT_80615730;

    int desc = *DAT_80610a20 + sprite * 0x58;
    uint tw = (uint)*(unsigned short*)(desc + 0x4e) << 4;
    uint th = (uint)*(unsigned short*)(desc + 0x52) << 4;
    FUN_8016e77c(sprite);

    uint alpha = (uint)((float)((double)(baseColor & 0xff) - DOUBLE_80615740) * (float)((double)FLOAT_80615754 - p2));
    if ((int)alpha > 0x7f) alpha = 0x7f;
    uint color = baseColor & 0xffffff00 | alpha;

    int culled;
    if (entArg == 0) {
        // world-point path: project via the view matrix and test against frustum
        FUN_8012fad0(*(float**)(DAT_80611d54 + 0x20c), pt, proj + 3);
        float w = -proj[2];
        if (((proj[3] < w) || (proj[2] < proj[3])) || (proj[0] < w) ||
            (proj[2] < proj[0]) || (proj[1] < w) || (proj[2] < proj[1])) {
            culled = 1;
        } else {
            culled = 0;
            float invW = FLOAT_806159a8 / proj[2];
            float sx = (float)((double)(*(int*)(DAT_80611748 + 8)) - DOUBLE_80615738) +
                       (proj[3] * (float)((double)(*(int*)(DAT_80611748 + 0x10)) - DOUBLE_80615738)) / proj[2];
            float sy = (float)((double)(*(int*)(DAT_80611748 + 0x1c)) - DOUBLE_80615738) -
                       (proj[0] * (float)((double)(*(int*)(DAT_80611748 + 0x24)) - DOUBLE_80615738)) / proj[2];
            x0 = (uint)(sx - invW); x1 = (uint)(sx + invW);
            y0 = (uint)(sy - invW); y1 = (uint)(sy + invW);
            float dx = pt[0] - *(float*)(DAT_80611d54 + 0x27c);
            float dy = pt[1] - *(float*)(DAT_80611d54 + 0x280);
            float dz = pt[2] - *(float*)(DAT_80611d54 + 0x284);
            dist = FUN_80131538((double)(dz * dz + dx * dx + dy * dy));
        }
    } else {
        // entity path: use the entity's screen bounds
        int ok = entity_compute_screen_bounds(entArg, &x0, (int*)&y0, (int*)&x1, (int*)&y1, (float*)0, 0);
        if (ok == 0) {
            culled = 1;
        } else {
            float dx = (*(float*)(entArg + 100) + *(float*)(entArg + 0x1a8)) - *(float*)(DAT_80611d54 + 0x27c);
            float dy = (*(float*)(entArg + 0x68) + *(float*)(entArg + 0x1ac)) - *(float*)(DAT_80611d54 + 0x280);
            float dz = (*(float*)(entArg + 0x6c) + *(float*)(entArg + 0x1b0)) - *(float*)(DAT_80611d54 + 0x284);
            dist = FUN_80131538((double)(dz * dz + dx * dx + dy * dy));
            culled = 0;
        }
    }

    if (culled == 0) {
        // fade small boxes by screen-area ratio
        uint boxArea = (x1 - x0) * (y1 - y0);
        uint screenArea = DAT_806109c4 * DAT_806109c8;
        float areaF = (float)((double)CONCAT44(0x43300000, boxArea ^ 0x80000000) - DOUBLE_80615738);
        if ((float)((double)CONCAT44(0x43300000, screenArea ^ 0x80000000) - DOUBLE_80615738) <= areaF) {
            FUN_80382708();
            return 1;
        }
        float halfScreen = (float)((double)CONCAT44(0x43300000, screenArea ^ 0x80000000) - DOUBLE_80615738) * FLOAT_80615734;
        if (halfScreen < areaF) {
            float ratio = -(FLOAT_80615748 * (areaF / (float)((double)CONCAT44(0x43300000, screenArea ^ 0x80000000) - DOUBLE_80615738) - FLOAT_80615734) - FLOAT_80615718);
            float clampLo = (FLOAT_80615730 < ratio) ? ratio : FLOAT_80615730;
            float clampHi = (clampLo <= FLOAT_80615718) ? ((FLOAT_80615730 < ratio) ? ratio : FLOAT_80615730) : FLOAT_80615718;
            uint av = (uint)((float)((double)(alpha & 0xff) - DOUBLE_80615740) * clampHi);
            if ((int)av > 0x7f) av = 0x7f;
            color = baseColor & 0xffffff00 | (alpha & 0xffffff00) | av;
        }

        // pad the bounds outward, then enforce a minimum size
        x0 -= 0x1e; x1 += 0x1e; y0 -= 0x1e; y1 += 0x1e;
        float wpx = (float)((double)CONCAT44(0x43300000, (x1 - x0) ^ 0x80000000) - DOUBLE_80615738);
        if (wpx < FLOAT_806157b4) {
            float grow = (FLOAT_806157b4 - wpx) * FLOAT_80615734;
            x1 = (uint)((float)((double)CONCAT44(0x43300000, x1 ^ 0x80000000) - DOUBLE_80615738) + grow);
            x0 = (uint)((float)((double)CONCAT44(0x43300000, x0 ^ 0x80000000) - DOUBLE_80615738) - grow);
        }
        float hpx = (float)((double)CONCAT44(0x43300000, (y1 - y0) ^ 0x80000000) - DOUBLE_80615738);
        if (hpx < FLOAT_806157b4) {
            float grow = (FLOAT_806157b4 - hpx) * FLOAT_80615734;
            y1 = (uint)((float)((double)CONCAT44(0x43300000, y1 ^ 0x80000000) - DOUBLE_80615738) + grow);
            y0 = (uint)((float)((double)CONCAT44(0x43300000, y0 ^ 0x80000000) - DOUBLE_80615738) - grow);
        }

        uint cornerColor = color;
        if (expanding != 0) {
            uint av = (uint)((float)((double)(color & 0xff) - DOUBLE_80615740) *
                             -(FLOAT_80615748 * (float)(p2 - (double)FLOAT_80615734) - FLOAT_80615718));
            if ((int)av > 0x7f) av = 0x7f;
            cornerColor = color & 0xffffff00 | av;
        }

        // four corner brackets
        FUN_80170410(x0, y0, x0 + 0x20, y0 + 0x20, 0, th, tw, 0);
        FUN_80170410(x1 - 0x20, y0, x1, y0 + 0x20, tw, th, 0, 0);
        FUN_80170410(x0, y1 - 0x20, x0 + 0x20, y1, 0, 0, tw, th);
        FUN_80170410(x1 - 0x20, y1 - 0x20, x1, y1, tw, 0, 0, th);

        if ((entArg == 0) || ((*(uint*)(entArg + 0x28) & 0x800000) == 0)) {
            // animated frame + gradient edge panels + text readout
            int frameTex = sprite_get_texture_id(0x128);
            FUN_8016e77c(frameTex);
            int fdesc = *DAT_80610a20 + frameTex * 0x58;
            uint ftw = (uint)*(unsigned short*)(fdesc + 0x4e) << 4;
            uint fth = (uint)*(unsigned short*)(fdesc + 0x52) << 4;
            uint swz = color << 0x18 | (color & 0xff00) << 8 | color >> 0x18 | color >> 8 & 0xff00;

            // top/bottom edge fills
            FUN_80170410(x0 + 0x20, y0, x1 - 0x20, y0 + 0x20, 0, 0, ftw, fth);
            FUN_80170410(x0 + 0x20, y1 - 0x20, x1 - 0x20, y1, 0, 0, ftw, fth);
            // left/right gradient panels
            FUN_80170dbc((double)FLOAT_80615730, (double)x0, (double)(y0 + 0x20), (double)(y1 - 0x20),
                         (double)(y1 - 0x20), (double)x0, (double)(x0 + 0x20), (double)(y0 + 0x20),
                         0, 0, 0, fth, ftw, 0, ftw, fth, (float)(x0 + 0x20), swz, swz, swz, swz);
            FUN_80170dbc((double)FLOAT_80615730, (double)x1, (double)(y0 + 0x20), (double)(y1 - 0x20),
                         (double)(y1 - 0x20), (double)x1, (double)(x1 - 0x20), (double)(y0 + 0x20),
                         0, 0, 0, fth, ftw, 0, ftw, fth, (float)(x1 - 0x20), swz, swz, swz, swz);

            // distance / bearing / coordinate text
            int token = leaf_92c0(1);
            text_set_font_table(9);
            double scale = (double)((float)((double)CONCAT44(0x43300000, (x1 - x0) ^ 0x80000000) - DOUBLE_80615738) / FLOAT_806159ac);
            text_set_color_rgb(scale, scale, scale);
            unsigned char* dg = FUN_8012e878(dist, scale, scale, 0, 0, 0, 0, 0, (void*)DAT_806108a8, 0, 0, 0, 0, 0, 0, 0);
            text_draw_char((double)(float)((double)CONCAT44(0x43300000, (x0 + 4) ^ 0x80000000) - DOUBLE_80615738),
                           (double)(float)((double)CONCAT44(0x43300000, (y0 + 4) ^ 0x80000000) - DOUBLE_80615738),
                           dg, alpha);

            double bearing = (double)FUN_80390aa4(
                (double)(float)((double)*(float*)(DAT_80611d54 + 0x284) - (double)pt[2]),
                (double)(*(float*)(DAT_80611d54 + 0x27c) - pt[0]));
            int deg = (int)((double)(float)((double)FLOAT_80615848 * bearing) / (double)FLOAT_8061572c);
            if (deg < 0) deg += 0x168;
            unsigned char* bg = FUN_8012e878((double)(float)((double)FLOAT_80615848 * bearing), bearing, 0, 0, 0, 0, 0, 0,
                                             (void*)DAT_806108b0, deg, 0, 0, 0, 0, 0, 0);
            double bw = text_get_char_width(bg);
            text_draw_char((double)(float)((double)(float)((double)CONCAT44(0x43300000, (x1 - 4) ^ 0x80000000) - DOUBLE_80615738) - bw),
                           (double)(float)((double)CONCAT44(0x43300000, (y0 + 4) ^ 0x80000000) - DOUBLE_80615738),
                           bg, alpha);

            // three random coordinate digits (jittered each few frames)
            if (DAT_80611928 % 5 < DAT_806108e4) {
                for (int i = 0; i < 3; i++) proj[i] = (float)FUN_8013104c();
            }
            int rnd = FUN_8013104c();
            unsigned char* cg = FUN_8012e878((double)rnd, 0, 0, 0, 0, 0, 0, 0, (void*)DAT_806108b8, rnd, DAT_80611928, 0, 0, 0, 0, 0);
            double ch = text_get_char_height((unsigned char*)DAT_80610878);
            text_draw_char((double)(float)((double)CONCAT44(0x43300000, (x0 + 4) ^ 0x80000000) - DOUBLE_80615738),
                           (double)(float)((double)(float)((double)CONCAT44(0x43300000, (y1 - 4) ^ 0x80000000) - DOUBLE_80615738) - ch),
                           cg, alpha);

            double one = (double)FLOAT_80615718;
            text_set_color_rgb(one, one, one);
            leaf_92c0(token);
        }
    }
    FUN_80382708();
    return (culled == 0) ? 1 : 0;
}
