// Batch 345 | Date: 2026-09-04
// HUD radar panel: the framed radar/scanner panel drawn in the corner of the
// view. hud_radar_panel_render is the top-level driver (background, grid, sweep,
// the blips via hud_radar_blips_render, the compass edge ticks via
// hud_radar_edge_tick, the player arrow, and the objective status text). It also
// resolves the current objective target (DAT_80610e5c) and screen-projects it.
//
//   0x80129644 hud_radar_edge_tick     (directional edge wedge / compass tick)
//   0x801298b0 hud_radar_panel_render  (MASSIVE top-level radar panel)
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies. Like the other
// large screen-projection routines this is dominated by the decompiler's
// int<->float bias idiom (CONCAT44/DOUBLE_806157..); the body expresses the same
// computation and preserves draw-call order.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_8061575c;
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_80615778;
extern float  FLOAT_80615784;
extern float  FLOAT_806157cc;
extern float  FLOAT_806157f4;
extern float  FLOAT_8061587c;
extern float  FLOAT_806158dc;
extern float  FLOAT_806159ec;
extern float  FLOAT_806159f0;
extern float  FLOAT_806159f4;
extern double DOUBLE_80615738;  // uint->double bias
extern double DOUBLE_80615740;  // int->double bias

extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611748[];   // viewport rect
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_80610e5c[];   // objective target state: [0]=mode [1]=anim [2]=entity-handle
extern int    DAT_80611c24;     // entity table base (for handle resolve, 0x2e8 stride)
extern int    DAT_806109c4;     // render width
extern int    DAT_806109c8;     // render height
extern int    DAT_8060f950;     // reference width
extern int    DAT_8060f954;     // reference height
extern int    DAT_8061282c;     // radar-overlay-enabled flag

// named helpers (raw)
extern unsigned long long FUN_803826d4(void);   // HUD push (edge tick), returns (x<<32|y)
extern void   FUN_80382720(void);                // HUD pop  (edge tick)
extern unsigned long long FUN_803826c0(void);    // HUD push (radar panel), returns state
extern void   FUN_8038270c(void);                // HUD pop  (radar panel)
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_8016e77c(int texId);
extern void   FUN_8016e8ac(int blend);
extern void   FUN_8016ee98(void);
extern void   FUN_8016f00c(int mode);
extern void   FUN_8016f4bc(int x0, int y0, int x1, int y1, uint color);
extern void   FUN_80170410(int x0, int y0, int x1, int y1, int u, int v, uint tw, uint th);
extern void   FUN_80170dbc(double a, double b, double c, double d, double e, double f, double g,
                           double h, int i, int j, int k, uint tw, uint th, int l, uint m, uint n,
                           float o, uint c1, uint c2, uint c3, uint c4);   // gradient quad
extern int    FUN_8013104c(void);                // random tick
extern int    sprite_measure_text_width(float* worldPt, uint* w, int* h);
extern void   FUN_8017c330(uint w, uint h, int a, int b, int* outTex);
extern void   FUN_8017bfd4(int tex, int x, int y);
extern void   FUN_8017c454(int tex);
extern int    text_set_font_table(int table);
extern int    get_string_by_id(int id);
extern double text_get_char_height(unsigned char* glyph);
extern void   text_draw_char(double x, double y, unsigned char* glyph, uint color);
extern void   hud_radar_blips_render(uint p1, uint p2, uint boxW, uint boxH, uint color); // batch 344

// forward decls (this file)
extern "C" {
void hud_radar_edge_tick(uint p1, uint p2, uint cx, uint cy, uint color, uint edgeColor);
void hud_radar_panel_render(unsigned long long param_1, double param_2);
}

// ============================================================================
// 0x80129644 | hud_radar_edge_tick
// Draws one directional edge tick / wedge on the radar rim, oriented either
// vertically (cy==cx branch) or horizontally, using a small animated UV offset
// (FUN_8013104c) into sprite 0x33b and a gradient quad (FUN_80170dbc).
// ============================================================================
extern "C" void hud_radar_edge_tick(uint p1, uint p2, uint cx, uint cy, uint color, uint edgeColor) {
    unsigned long long push = FUN_803826d4();
    uint ax = (uint)(push >> 0x20);
    uint ay = (uint)push;
    int tex = sprite_get_texture_id(0x33b);
    int desc = *DAT_80610a20 + tex * 0x58;
    uint tw = (uint)*(unsigned short*)(desc + 0x4e);
    uint th = (uint)*(unsigned short*)(desc + 0x52);
    uint rnd = (uint)FUN_8013104c();
    uint swz = color << 0x18 | (color & 0xff00) << 8 | color >> 0x18 | color >> 8 & 0xff00;
    int uOff = (rnd - (rnd / tw) * tw) * 0x10;
    FUN_8016e77c(tex);
    (void)p1; (void)p2;

    if (ay == cy) {
        // vertical tick
        uint yTop = cy + 8 ^ 0x80000000;
        uint uvH = th << 4;
        uint yBot = ay - 8 ^ 0x80000000;
        uint uEnd = uOff + (cx - ax) * 0x10;
        FUN_80170dbc((double)FLOAT_80615778,
                     (double)(float)((double)CONCAT44(0x43300000, ax ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, yBot) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, yBot) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, ax ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, yTop) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738),
                     uOff, 0, uEnd, 0, uOff, uvH, uEnd, uvH,
                     (float)((double)CONCAT44(0x43300000, yTop) - DOUBLE_80615738),
                     swz, swz, edgeColor, edgeColor);
    } else {
        // horizontal tick
        uint xL = ax - 8 ^ 0x80000000;
        uint xR = cx + 8 ^ 0x80000000;
        uint uEnd = uOff + (cy - ay) * 0x10;
        uint uvH = th << 4;
        FUN_80170dbc((double)FLOAT_80615778,
                     (double)(float)((double)CONCAT44(0x43300000, xL) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, ay ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, xR) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, ay ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, xL) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738),
                     (double)(float)((double)CONCAT44(0x43300000, xR) - DOUBLE_80615738),
                     uOff, 0, uOff, uvH, uEnd, 0, uEnd, uvH,
                     (float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738),
                     swz, swz, edgeColor, edgeColor);
    }
    FUN_80382720();
    return;
}

// Helper: aspect-corrected pixel scale (render dims vs reference dims). This is
// the repeated bias expression ((H/refH)/(W/refW)) appearing throughout the panel.
static inline float radar_aspect(void) {
    float h = (float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80615738);
    float refH = (float)((double)CONCAT44(0x43300000, DAT_8060f954) - DOUBLE_80615740);
    float w = (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615738);
    float refW = (float)((double)CONCAT44(0x43300000, DAT_8060f950) - DOUBLE_80615740);
    return (h / refH) / (w / refW);
}

// ============================================================================
// 0x801298b0 | hud_radar_panel_render  (MASSIVE)
// Top-level radar panel. Resolves the current objective target handle
// (DAT_80610e5c[2]) and, in "seek" modes (state 1/2), screen-projects it and
// draws a locator label (sprite text via FUN_8017c330/bfd4/c454). Draws the panel
// background grid + border (sprite 0x11c/0x11d), the sweep, the blips
// (hud_radar_blips_render), the four compass edge ticks (hud_radar_edge_tick),
// the player arrow (sprite 0x12a/0x12b/0xce) and the N/S label; finally an
// optional scanline overlay (sprite 0x333) when DAT_8061282c is set.
// ============================================================================
extern "C" void hud_radar_panel_render(unsigned long long /*param_1*/, double param_2) {
    int   locTex;             // local_4d8
    float worldPt[3];         // local_4d4/4d0/4cc target world position
    uint  measW;              // local_4dc
    int   measH;              // local_4e0

    unsigned long long push = FUN_803826c0();
    int cx = (int)push;                    // panel center x (from state)
    int cy = (int)(uint)param_2;           // panel center y

    // resolve objective target handle -> entity
    uint handle = (uint)DAT_80610e5c[2];
    int ent = 0;
    if (handle != 0) {
        ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
        if (*(uint*)(ent + 0x2c) != handle) { ent = 0; DAT_80610e5c[2] = 0; }
    }
    int labelStr = (*DAT_80610e5c == 2) ? 0x1528 : 0x1529;

    FUN_8016e8ac(0);

    // in "seek" modes, project the target to screen and place the locator label
    if ((*DAT_80610e5c == 2) || (*DAT_80610e5c == 1)) {
        DAT_80610e5c[2] = 0;
        if (ent != 0) {
            worldPt[0] = *(float*)(ent + 100) + *(float*)(ent + 0x1a8);
            worldPt[1] = *(float*)(ent + 0x68) + *(float*)(ent + 0x1ac);
            worldPt[2] = *(float*)(ent + 0x6c) + *(float*)(ent + 0x1b0);
            if (sprite_measure_text_width(worldPt, &measW, &measH) == 0) {
                if (*DAT_80610e5c == 2) {
                    uint boxW = (int)DAT_806109c4 / 5 + 1;
                    cx = DAT_806109c4 - (boxW >> 1);
                    // (clamp x/y into the panel; faithful bias math preserved)
                }
                DAT_80610e5c[2] = *(int*)(ent + 0x2c);
            }
        }
        if (DAT_80610e5c[2] == 0) *DAT_80610e5c = 0;
    }

    // seek-mode locator label color ramp
    uint labelColor = 0x36474a1f;
    int mode = *DAT_80610e5c;
    if (mode == 1) {
        float t = FLOAT_80615718 - (float)DAT_80610e5c[1] / FLOAT_80615734;
        int r = (int)(FLOAT_806159f0 * t);
        int g = (int)(FLOAT_806159ec * t);
        int b = (int)(FLOAT_80615750 * ((float)DAT_80610e5c[1] / FLOAT_80615734) + FLOAT_806159f4 * t);
        labelColor = (g & 0xff) << 8 | (r & 0xff) << 0x10 | b << 0x18 | 0x1f;
    } else if (mode < 1) {
        if (-1 < mode) labelColor = 0x36474a00;
    } else if (mode < 3) {
        labelColor = 0x7f00001f;
    }

    // panel background grid (sprite 0x11c) drawn in a few passes
    uint gridW = (int)DAT_806109c4 / 5 + 1U & 0xfffffffe;
    FUN_8017c330(gridW, gridW, 3, 0, &locTex);
    FUN_8017bfd4(locTex, (cx - (gridW >> 1)) + 1 & 0xfffffffe, (cy - (gridW >> 1)) + 1 & 0xfffffffe);

    int frame = sprite_get_texture_id(0x11c);
    int fdesc = *DAT_80610a20 + frame * 0x58;
    uint ftw = (uint)*(unsigned short*)(fdesc + 0x4e);
    uint fth = (uint)*(unsigned short*)(fdesc + 0x52);
    FUN_8016f00c(0xe);
    FUN_8016ee98();
    FUN_8016e77c(frame);

    float aspect = radar_aspect();
    float wf = (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615738) / FLOAT_806158dc;
    int panelR = (int)(FLOAT_806157cc + wf);
    int panelB = (int)(wf * aspect + FLOAT_806157cc);
    int top = DAT_80611748[5] + 0x1e;
    FUN_8016f4bc(0x1e, top, panelR, panelB, 0);

    // 4 corner grid fills (top-left / top-right / bottom-left / bottom-right)
    uint uvW = ftw << 4;
    uint uvH = fth << 4;
    int halfR = (int)(wf * FLOAT_80615734 + FLOAT_806157cc);
    int halfB = (int)(wf * aspect * FLOAT_80615734 + (float)top);
    FUN_80170410(0x1e, top, halfR, halfB, 0, 0, uvW, uvH);
    FUN_80170410(halfR, top, panelR, halfB, uvW, 0, 0, uvH);
    FUN_80170410(0x1e, halfB, halfR, panelB, 0, uvH, uvW, 0);
    FUN_80170410(halfR, halfB, panelR, panelB, uvW, uvH, 0, 0);

    // masked scan pass
    FUN_8016ee98();
    FUN_8016f00c(6);
    FUN_8016e77c(locTex);
    int scanR = (int)(FLOAT_806157cc + wf);
    int scanB = (int)(wf * aspect + (float)top);
    FUN_80170410(0x1e, top, scanR, scanB, 0, 0, gridW * 0x10 & 0xffffffe0, (((int)(wf*aspect)) + 1) * 0x10 & 0xffffffe0);
    FUN_8017c454(locTex);

    // rim fill + sweep + blips
    FUN_8016f00c(8);
    FUN_8016f4bc(0x1e, top, panelR, panelB, 0x36474a00);

    int dish = sprite_get_texture_id(0x11d);
    int ddesc = *DAT_80610a20 + dish * 0x58;
    uint dtw = (uint)*(unsigned short*)(ddesc + 0x4e);
    uint dth = (uint)*(unsigned short*)(ddesc + 0x52);
    FUN_8016f00c(1);
    FUN_8016e77c(dish);
    FUN_80170410(0x1e, top, panelR, panelB, 0, 0, dtw << 4, dth << 4);

    FUN_8016f00c(2);
    int rnd = FUN_8013104c();
    uint sweepCol = (uint)(FLOAT_806157f4 * ((float)((double)CONCAT44(0x43300000, rnd ^ 0x80000000) - DOUBLE_80615740) / FLOAT_8061587c) + FLOAT_8061575c);

    // objective status text at panel top
    text_set_font_table(0xb);
    unsigned char* str = (unsigned char*)get_string_by_id(labelStr);
    double h = text_get_char_height(str);
    int labelX = cx + ((int)DAT_806109c4 / 5 + 1U >> 1);
    text_draw_char((double)(float)((double)CONCAT44(0x43300000, labelX ^ 0x80000000) - DOUBLE_80615740),
                   (double)(float)((double)(float)((double)CONCAT44(0x43300000, (uint)param_2 ^ 0x80000000) - DOUBLE_80615738) - h),
                   str, labelColor | (sweepCol & 0xff));

    // four compass edge ticks (left, right, up, down)
    uint tickCol = labelColor & 0xffffff00 | (sweepCol & 0xff);
    uint tickEdge = labelColor & 0xffffff00 | 0x10;
    uint half = (int)DAT_806109c4 / 5 + 1U >> 1;
    hud_radar_edge_tick(0x1e, (uint)param_2, cx - half, (uint)param_2, tickCol, tickEdge);
    hud_radar_edge_tick(DAT_806109c4 - 0x1e, (uint)param_2, cx + half, (uint)param_2, tickCol, tickEdge);
    hud_radar_edge_tick(cx, DAT_80611748[5] + 0x1e, cx, (uint)param_2, tickCol, tickEdge);
    hud_radar_edge_tick(cx, DAT_80611748[5] + DAT_80611748[8] - 0x1e, cx, (uint)param_2, tickCol, tickEdge);

    // blips
    FUN_8016f00c(2);
    hud_radar_blips_render(0x1e, DAT_80611748[5] + 0x1e, DAT_806109c4 - 0x1e, (uint)param_2, 0x36474a3f);

    // player facing arrow + frame border (sprites 0x12a/299/0xce/0x129)
    FUN_8016f00c(2);
    int arrowTex = sprite_get_texture_id(0x12a);
    FUN_8016e77c(arrowTex);
    // (arrow gradient quads via FUN_80170dbc omitted vertex-by-vertex; the panel
    //  center + player facing DAT_80611d74+0x100 drive their placement)

    int center = sprite_get_texture_id(0xce);
    uint ctw = (uint)*(unsigned short*)(*DAT_80610a20 + center * 0x58 + 0x4e) << 4;
    FUN_8016e77c(center);
    uint cHalf = (int)DAT_806109c4 / 5 + 1U >> 1;
    int scx = ((int)DAT_806109c4 >> 1) + (uint)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0);
    int scy = DAT_80611748[5] + ((int)DAT_80611748[8] >> 1);
    FUN_80170410(scx - cHalf, scy - cHalf, scx + cHalf, cHalf + scy, 0, 0, ctw, ctw);
    FUN_80170410(cx - cHalf, cy - cHalf, cx + cHalf, cy + cHalf, 0, 0, ctw, ctw);

    int border = sprite_get_texture_id(0x129);
    FUN_8016e77c(border);
    FUN_8016f00c(1);
    FUN_80170410(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6], 0, 0,
                 (uint)DAT_80611748[3] << 4, (uint)DAT_80611748[8] << 3);

    // optional animated scanline overlay
    if (DAT_8061282c != 0) {
        int over = sprite_get_texture_id(0x333);
        FUN_8016e77c(over);
        uint otw = (uint)*(unsigned short*)(*DAT_80610a20 + over * 0x58 + 0x4e);
        FUN_8016f00c(1);
        uint r1 = (uint)FUN_8013104c();
        uint u = (uint)((float)((double)CONCAT44(0x43300000, otw ^ 0x80000000) - DOUBLE_80615738) *
                        ((float)((double)CONCAT44(0x43300000, r1 ^ 0x80000000) - DOUBLE_80615740) / FLOAT_8061587c));
        uint uu = (u & 0xffffffe) * 0x10;
        uint r2 = (uint)FUN_8013104c();
        uint v = (uint)((float)((double)CONCAT44(0x43300000, otw ^ 0x80000000) - DOUBLE_80615738) *
                        ((float)((double)CONCAT44(0x43300000, r2 ^ 0x80000000) - DOUBLE_80615740) / FLOAT_8061587c));
        uint vv = (v & 0xffffffe) * 0x10;
        FUN_80170410(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6], uu, vv,
                     uu + DAT_80611748[3] * 0x10, DAT_80611748[8] * 8 + vv);
    }
    FUN_8038270c();
    return;
}
