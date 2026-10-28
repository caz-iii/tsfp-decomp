// Batch 344 | Date: 2026-09-04
// HUD vehicle gauges + capture-point labels. Driven per-frame from the HUD tick:
//
//   0x80128a7c hud_draw_labeled_gauge   (bar-needle gauge with a centered label)
//   0x801285e4 hud_capture_point_labels (CTGT capture-point names + pickup hint)
//   0x80128de0 hud_vehicle_gauges_render (dispatcher: speed gauge / turn gauge
//                                         via hud_draw_labeled_gauge, then labels)
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_80615754;
extern float  FLOAT_80615768;
extern float  FLOAT_80615794;
extern float  FLOAT_806157ac;
extern float  FLOAT_806157c0;
extern float  FLOAT_806157c4;
extern float  FLOAT_806157f0;
extern float  FLOAT_806158b4;
extern float  FLOAT_8061586c;
extern float  FLOAT_80615848;
extern float  FLOAT_80615910;
extern float  FLOAT_8061082c;
extern float  FLOAT_80610830;
extern float  FLOAT_80611674;
extern double DOUBLE_80615738;  // uint->double bias

extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611d54;     // active render/camera context
extern int    DAT_80611748[];   // viewport rect
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_8057ff4c, DAT_8057ff54, DAT_8057ff58; // layout insets
extern int    DAT_80610838;     // default HUD font table
extern int    DAT_80611928;     // frame counter
extern int    DAT_80612610;     // active player count
extern int    DAT_80612098;     // tracked-entity count
extern float  DAT_80583228;     // sine LUT (indexed via &NAME)
extern int    DAT_80588610;     // tracked-entity ptr table (via &NAME)
extern float  DAT_8040d2bc;     // capture-label local positions table (0x14 stride, via &NAME)
extern int    DAT_8040d2b8;     // capture-label id table (0x14 stride, via &NAME)
extern char   DAT_80610864[];   // capture-label glyph template
extern char   DAT_806108c0[];   // gauge-label glyph template

// named helpers (raw)
extern unsigned long long FUN_803826e0(void);   // HUD push (gauge/label), returns state
extern void   FUN_8038272c(void);                // HUD pop  (gauge/label)
extern void   FUN_803826d8(void);                // HUD push (capture labels)
extern void   FUN_80382724(void);                // HUD pop  (capture labels)
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_8016e77c(int texId);
extern void   FUN_8016e84c(void);
extern int    FUN_8016f00c(int mode);
extern void   FUN_80170b10(double x0, double y0, double x1, double y1, int u, int v, int tw, int th);
extern int    FUN_803825c0(double v);            // float -> clamped byte
extern void   hud_draw_bar_needle(double p1, double p2, double p3, double p4, double p5,
                                  double p6, double p7, double p8, int a, int b, int c);
extern int    text_set_font_table(int table);
extern double text_get_char_width(unsigned char* glyph);
extern double text_get_char_height(unsigned char* glyph);
extern void   text_apply_alpha(double a, unsigned char* glyph, int flags);
extern int    text_set_color_rgb(double r, double g, double b);
extern void   text_draw_char(double x, double y, unsigned char* glyph, uint color);
extern void   text_draw_char_shadowed(double x, double y, unsigned char* glyph, uint color, uint shadow);
extern int    text_get_dimensions_int(int* w, int* h, unsigned char* glyph);
extern void   input_get_button_name(int button, int* out_glyph);
extern void   FUN_8038b510(int* dst_glyph, int* src);
extern int    get_string_by_id(int id);
extern int    spawn_point_find_by_entity(int tag);
extern int    FUN_8002588c(int a, int b);
extern void   FUN_8012fa34(float* mtx, float* vec);
extern void   FUN_8012fb70(float* mtx, float* vecIn, float* vecOut);
extern unsigned char* FUN_8012e878(double, double, double, double, double, double, double, double,
                                   void* fmt, int a, int b, int c, int d, int* e, int f, int g);
extern void   FUN_8038b4a0(int glyph, int append);
extern double hud_row_height_scale(void);        // 0x80227e.. row-height scale (returns double)
extern void   FUN_80227e54(int* mode, float* turn, float* out);

// forward decls (this file)
extern "C" {
void hud_draw_labeled_gauge(double p1, double p2, double p3, double p4, double p5,
                            double p6, double p7);
void hud_capture_point_labels(void);
void hud_vehicle_gauges_render(void);
}

// ============================================================================
// 0x80128a7c | hud_draw_labeled_gauge
// Draws a horizontal bar-needle gauge (sprite 0xdb frame corners + needle via
// hud_draw_bar_needle) inside the rect (p2,p3)-(p4,p5) with fill fraction p6 and
// alpha p7, then draws a centered label (the glyph passed in the HUD state) with
// a shadow beneath the bar.
// ============================================================================
extern "C" void hud_draw_labeled_gauge(double /*p1*/, double p2, double p3, double p4, double p5,
                                       double p6, double p7) {
    unsigned long long st = FUN_803826e0();
    unsigned char* label = (unsigned char*)(uintptr_t)st;
    double sy = (double)FLOAT_8061082c;
    double sx = (double)FLOAT_80610830;
    double zero = 0.0;

    int tex = sprite_get_texture_id(0xdb);
    int desc = *DAT_80610a20 + tex * 0x58;
    uint th = (uint)*(unsigned short*)(desc + 0x52);
    double inset = (double)(float)((double)FLOAT_80615768 * sx);
    uint tw = (uint)*(unsigned short*)(desc + 0x4e);
    double x0 = (double)(float)(p2 - inset);
    double x1 = (double)(float)(p4 + inset);
    double frameH = (double)(float)(sx * (double)(float)((double)(float)((double)CONCAT44(0x43300000, th ^ 0x80000000) - DOUBLE_80615738) - (double)FLOAT_80615768));
    double frameW = (double)(float)(sy * (double)(float)((double)CONCAT44(0x43300000, tw << 1 ^ 0x80000000) - DOUBLE_80615738));
    double ix0 = (double)(float)(p2 + frameH);
    double ix1 = (double)(float)(p4 - frameH);
    double iy = (double)(float)(zero + frameW);
    double y0 = (double)(float)(p3 - frameW);
    uint alpha = FUN_803825c0((double)(float)((double)FLOAT_806157c4 * p7));

    FUN_8016e77c(tex);
    int uv = tw << 5;
    uint uvH = th << 4;
    FUN_80170b10(zero, x0, iy, ix0, uv, 0, 0, uvH);
    FUN_80170b10(p3, x0, y0, ix0, uv, 0, 0, uvH);
    FUN_80170b10(zero, x1, iy, ix1, uv, 0, 0, uvH);
    FUN_80170b10(p3, x1, y0, ix1, uv, 0, 0, uvH);
    hud_draw_bar_needle(zero, p2, p3, p4, p5, (double)FLOAT_8061586c, (double)FLOAT_8061586c, p6,
                        (int)(st >> 0x20), 1, 0);

    if (label != (unsigned char*)0) {
        int savedFont = text_set_font_table(2);
        double lw = text_get_char_width(label);
        double lx = (double)(FLOAT_80615734 * (float)((double)(float)(zero + p3) - lw));
        double lh = text_get_char_height(label);
        double ly = (double)(float)(iy - lh);
        text_apply_alpha((double)(float)(y0 - iy), label, 0);
        uint col = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * p7));
        text_draw_char_shadowed(lx, ly, label, col & 0xff | 0x7f7f7f00, alpha & 0xff);
        double one = (double)FLOAT_80615718;
        text_set_color_rgb(one, one, one);
        text_set_font_table(savedFont);
    }
    FUN_8038272c();
    return;
}

// ============================================================================
// 0x801285e4 | hud_capture_point_labels
// When the active player is at the CTGT (capture-the-bag) staging area, draws
// the 7 capture-point name labels projected to screen space (FUN_8012e878 glyph
// compose + text_draw_char_shadowed), and if any player is carrying the bag,
// draws a pulsing "return" button hint at the bottom.
// ============================================================================
extern "C" void hud_capture_point_labels(void) {
    unsigned char btnGlyph[260];
    float local[6];   // local_1c0.. transformed point (+ projected local_1b4..)

    FUN_803826d8();
    bool carrying = false;
    uint leftEdge = DAT_8057ff4c + *DAT_80611748 + 10;
    int ctgt = spawn_point_find_by_entity(0x54475443);
    if (*(int*)(*(int*)(DAT_80611d74 + 8) + 0x26c) == ctgt) {
        int off = 0;
        for (int i = 0; i < 7; i++) {
            local[0] = *(float*)((int)&DAT_8040d2bc + off);
            local[1] = *(float*)((int)&DAT_8040d2bc + off + 4);
            local[2] = *(float*)((int)&DAT_8040d2bc + off + 8);
            FUN_8016e84c();
            FUN_8012fa34(*(float**)(DAT_80611d54 + 0x208), local);
            FUN_8012fb70(*(float**)(DAT_80611d54 + 0x204), local, local + 3);
            float projX = local[3];   // local_1b4
            float projY = local[4];   // local_1b0
            float projZ = local[5];   // local_1ac
            float projW = local[2];   // local_1a8 (w after transform; reused slot)
            if ((-projW < projZ) && (projZ < projW)) {
                double ratioZ = (double)(projZ / projW);
                double ndcX = (double)(projX / projW);
                double ndcY = (double)(projY / projW);
                uint rx = DAT_80611748[4] ^ 0x80000000;
                uint bx = DAT_80611748[2] ^ 0x80000000;
                uint ry = DAT_80611748[9] ^ 0x80000000;
                uint ty = DAT_80611748[7] ^ 0x80000000;
                double sx = (double)(float)((double)CONCAT44(0x43300000, rx) - DOUBLE_80615738);
                double sy = (double)(float)((double)CONCAT44(0x43300000, ry) - DOUBLE_80615738);
                double px = sx * ndcX + (double)(float)((double)CONCAT44(0x43300000, bx) - DOUBLE_80615738);
                int ix = (int)(float)px;
                int iy = (int)-(float)(sy * ndcY - (double)(float)((double)CONCAT44(0x43300000, ty) - DOUBLE_80615738));

                FUN_8016f00c(1);
                text_set_font_table(0x11);
                unsigned char* glyph = FUN_8012e878((double)(long long)ix, sy, px, ndcX, sx, 0, 0, 0,
                                                    (void*)DAT_80610864, (int)*(short*)((int)&DAT_8040d2b8 + off),
                                                    (int)bx, 0, (int)rx, DAT_80611748, 0, 0);
                int gw, gh;
                text_get_dimensions_int(&gw, &gh, glyph);
                uint cx = ix - ((gw >> 1) + (uint)(gw < 0 && (gw & 1) != 0));
                uint cy = iy - ((gh >> 1) + (uint)(gh < 0 && (gh & 1) != 0));
                if ((*DAT_80611748 < (int)cx) &&
                    ((int)cx < DAT_80611748[1]) && (DAT_80611748[5] < (int)cy) &&
                    ((int)cy < DAT_80611748[6]) && (ratioZ < (double)FLOAT_80615718)) {
                    short id = *(short*)((int)&DAT_8040d2b8 + off);
                    uint col = (id < 0) ? 0x7f0f0f64 : 0x40696964;
                    unsigned char* g2 = FUN_8012e878(0, sy, px, ndcX, sx, 0, 0, 0,
                                                     (void*)DAT_80610864, (int)id, (int)(intptr_t)DAT_80611748, 0, (int)rx, DAT_80611748, 0, 0);
                    double dx = (double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738);
                    double dy = (double)(float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738);
                    text_draw_char_shadowed(dx, dy, g2, col, 0x50);
                }
            }
            FUN_8016e84c();
            off += 0x14;
        }

        // carrying check across tracked entities
        int entOff = DAT_80612610 << 2;
        for (int i = DAT_80612610; i < DAT_80612098; i++) {
            int* ent = *(int**)((int)&DAT_80588610 + entOff);
            if ((ent[8] == 8) && (FUN_8002588c(*ent, 0x25) == 0)) {
                carrying = true;
                break;
            }
            entOff += 4;
        }

        if (carrying) {
            text_set_font_table(DAT_80610838);
            int* label = (int*)get_string_by_id(0x686);
            double bottomD = (double)(CONCAT44(0x43300000, DAT_80611748[6] - DAT_8057ff58) ^ 0x80000000);
            uint by = (uint)-(FLOAT_806158b4 * FLOAT_80611674 - (float)(bottomD - DOUBLE_80615738));
            input_get_button_name(0x10, (int*)btnGlyph);
            double bx = (double)(float)((double)CONCAT44(0x43300000, leftEdge ^ 0x80000000) - DOUBLE_80615738);
            double byD = (double)(float)((double)CONCAT44(0x43300000, by ^ 0x80000000) - DOUBLE_80615738);
            double phaseD = (double)(float)((double)CONCAT44(0x43300000, DAT_80611928 ^ 0x80000000) - DOUBLE_80615738);
            uint pi = (uint)(FLOAT_806157c0 * FLOAT_80615794 * phaseD + FLOAT_80615734);
            double pulse = (double)(FLOAT_80615734 * ((float)0.5 * (&DAT_80583228)[pi & 0x7ff] + FLOAT_80615734));
            text_draw_char(bx, byD, btnGlyph, 0x80);
            text_draw_char((double)(float)(bx - pulse), (double)(float)(byD - pulse), btnGlyph, 0x64646480);
            FUN_8038b510((int*)btnGlyph, label);
            double bx2 = (double)(float)((double)CONCAT44(0x43300000, leftEdge + 0x23 ^ 0x80000000) - DOUBLE_80615738);
            text_draw_char_shadowed(bx2, byD, btnGlyph, 0x64646480, 0x1e1e1e80);
        }
    }
    FUN_80382724();
    return;
}

// ============================================================================
// 0x80128de0 | hud_vehicle_gauges_render
// Vehicle gauge dispatcher: queries the vehicle telemetry (FUN_80227e54 -> mode,
// turn, speed). Mode 1 draws the speed gauge (label string 0x1601); mode 2 draws
// the turn/lean gauge (label string 0x15b9). Both route through
// hud_draw_labeled_gauge with a sine-LUT pulse. Then draws capture-point labels.
// ============================================================================
extern "C" void hud_vehicle_gauges_render(void) {
    int mode;
    float turn;
    float out[3];

    uint cxMag = DAT_8057ff4c + *DAT_80611748 +
                 ((DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) >> 1) ^ 0x80000000;
    double gx0 = -(double)(FLOAT_80615910 * FLOAT_8061082c - (float)((double)CONCAT44(0x43300000, cxMag) - DOUBLE_80615738));
    double rowH = hud_row_height_scale();
    int left = DAT_8057ff4c + *DAT_80611748;
    uint bottomMag = DAT_80611748[6] - DAT_8057ff58 ^ 0x80000000;
    uint cx2Mag = left + ((DAT_80611748[1] - DAT_8057ff54) - left >> 1) ^ 0x80000000;
    double bottomD = (double)(float)((double)CONCAT44(0x43300000, bottomMag) - DOUBLE_80615738);
    double gy1 = (double)(float)(bottomD - rowH);
    double gx1 = (double)(FLOAT_80615910 * FLOAT_8061082c + (float)((double)CONCAT44(0x43300000, cx2Mag) - DOUBLE_80615738));
    double gy0 = bottomD;

    FUN_80227e54(&mode, &turn, out);
    FUN_8016f00c(1);
    // (button glyph for the gauge label is composed inside hud_draw_labeled_gauge state)

    if (mode == 1) {
        double speed = (double)(FLOAT_806157c4 * out[0]);
        double half = (double)FLOAT_80615734;
        uint pi = (uint)((double)FLOAT_806157c0 * (double)(float)(speed / (double)FLOAT_80615848) + half);
        double s = (double)(float)(half * (double)(float)(&DAT_80583228)[pi & 0x7ff] + half);
        double clampHi = s; if ((double)FLOAT_80615718 < s) clampHi = (double)FLOAT_80615718;
        double frac = (double)FLOAT_80615730;
        if ((frac <= clampHi) && (frac = s, (double)FLOAT_80615718 < s)) frac = (double)FLOAT_80615718;
        int str = get_string_by_id(0x1601);
        int glyph = (int)(intptr_t)FUN_8012e878(speed, (double)FLOAT_806157c0, half, clampHi, 0, 0, 0, 0,
                                                (void*)DAT_806108c0, str, out ? 0 : 0, 0, left, 0, 0, 0);
        (void)glyph;
        hud_draw_labeled_gauge(gx0, gy1, gx1, gy0, frac, (double)FLOAT_80615730, (double)FLOAT_80615718);
    }
    if (mode == 2) {
        double c4 = (double)FLOAT_806157c4;
        double half = (double)FLOAT_80615734;
        double t = (double)(float)(c4 * -(double)turn);
        uint pi = (uint)((double)FLOAT_806157c0 * (double)(float)(t / (double)FLOAT_80615848) + half);
        double s = (double)(float)(half * (double)(float)(&DAT_80583228)[pi & 0x7ff]);
        double clampHi = s; if (half < s) clampHi = half;
        double frac = (double)FLOAT_8061586c;
        if ((frac <= clampHi) && (frac = s, (double)FLOAT_80615734 < s)) frac = (double)FLOAT_80615734;
        int str = get_string_by_id(0x15b9);
        int glyph = (int)(intptr_t)FUN_8012e878(t, c4, (double)FLOAT_806157c0, clampHi, 0, 0, 0, 0,
                                                (void*)DAT_806108c0, str, 0, 0, left, 0, 0, 0);
        (void)glyph;
        hud_draw_labeled_gauge(gx0, gy1, gx1, gy0, frac, (double)FLOAT_80615734, (double)FLOAT_80615718);
    }
    hud_capture_point_labels();
    return;
}
