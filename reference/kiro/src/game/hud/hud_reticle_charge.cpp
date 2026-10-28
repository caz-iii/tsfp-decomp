// Batch 342 | Date: 2026-09-04
// HUD reticle charge-meter, center overlay icon, vertical-scale helper, and the
// weapon/objective status panel. This group is driven from hud_reticle_render
// and hud_render_player_view:
//
//   0x801236f0 hud_charge_meter_draw     (MASSIVE: charge arc / bracket sprites)
//   0x80123b34 hud_reticle_center_icon   (pulsing center reticle icon, sprite 0x11e)
//   0x8012414c hud_scale_screen_height   (value * screen-height fraction helper)
//   0x8012429c hud_weapon_panel_draw     (MASSIVE: weapon/ammo panel quad + arrow)
//   0x80124910 hud_weapon_slot_sprite    (weapon slot -> HUD sprite id lookup)
//
// All read the active HUD view (DAT_80611d74) and viewport rect (DAT_80611748)
// and use the shared GX draw primitives (FUN_80170410 / FUN_8017a62c / etc.).
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies; every raw symbol
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// scalar float / double constants
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_8061571c;
extern float  FLOAT_80615720;
extern float  FLOAT_8061572c;
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_8061574c;
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_8061576c;
extern float  FLOAT_80615778;
extern float  FLOAT_80615784;
extern float  FLOAT_80615788;
extern float  FLOAT_8061579c;
extern float  FLOAT_806157a4;
extern float  FLOAT_806157a8;
extern float  FLOAT_806157ac;
extern float  FLOAT_806157b0;
extern float  FLOAT_806157c0;
extern float  FLOAT_806157c4;
extern float  FLOAT_806157c8;
extern float  FLOAT_806157cc;
extern float  FLOAT_80615814;
extern float  FLOAT_8061582c;
extern float  FLOAT_8061585c;
extern float  FLOAT_80615864;
extern float  FLOAT_80615880;
extern float  FLOAT_80615884;
extern float  FLOAT_806158d0;
extern float  FLOAT_806158ec;
extern float  FLOAT_806158f4;
extern float  FLOAT_806158f8;
extern float  FLOAT_806158fc;
extern float  FLOAT_80615900;
extern float  FLOAT_80615904;
extern float  FLOAT_8061594c;
extern float  FLOAT_80615964;
extern float  FLOAT_80615968;
extern float  FLOAT_8061596c;
extern float  FLOAT_80615970;
extern float  FLOAT_806108ec;
extern float  FLOAT_8061082c;
extern float  FLOAT_80610830;
extern double DOUBLE_80615738; // uint->double bias

// data / globals
extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611748[];   // viewport rect (also indexed via +offset elsewhere)
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_806109c0;     // frame delta accumulator (uint magnitude)
extern int    DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58; // layout insets
extern int    DAT_80611928;     // frame counter / animation phase
extern float  DAT_80583228;     // sine LUT (indexed via &NAME)
extern unsigned char DAT_80578684;   // per-view HUD scale table (0x177c stride, via &NAME)
extern int    DAT_80579dd0;     // secondary prompt-anim state table (unused type flag here)
extern int    DAT_80579de4;     // per-view reticle-arrow highlight ramp (0x177c stride, via &NAME)
extern unsigned char DAT_805015bd; // game mode byte
extern int    DAT_80612610;     // active player count
extern int    DAT_80611d78;     // player-slot table base
extern int    DAT_8043d99c[];   // weapon slot -> reticle sprite id table (5-word stride)
extern unsigned char DAT_8043e22c[]; // weapon slot -> flags table (0xf stride, bit 0x10 = hidden)

// named helpers (raw)
extern int    FUN_803826e4(void);            // HUD push (charge meter), returns state block ptr
extern int    FUN_801c1714(void);            // split-screen layout / player-count query
extern void   FUN_80382730(void);            // HUD pop  (charge meter)
extern void   FUN_803826e8(void);            // HUD push (center icon)
extern void   FUN_80382734(void);            // HUD pop  (center icon)
extern unsigned long long FUN_803826c4(void);// HUD push (weapon panel), returns rect
extern void   FUN_80382710(void);            // HUD pop  (weapon panel)
extern void   FUN_8016f00c(int mode);
extern void   FUN_8016e77c(int texId);
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_80170410(int x0, int y0, int x1, int y1, int u, int v, uint tw, uint th);
extern void   FUN_801708b4(double x0, double y0, double x1, double y1, int u, int v, uint tw, uint th);
extern void   FUN_8017a62c(uint tw, uint th, uint color, float* a, float* b, float* c, float* d);
extern int    FUN_803825c0(double v);        // float -> clamped byte
extern void   FUN_8001d030(double, double, double, double, double, double, double, double,
                           int, int, uint, uint, uint);      // textured quad (13-arg)
extern void   FUN_8016f0a0(int* a, int* b, int* c, int* d); // save scissor rect
extern void   FUN_8016f0e4(int x, int y, int w, int h);     // set scissor rect
extern void   sprite_draw_arrow_down(double x0, double y0, double x1, double y1);

// forward decls (this file)
extern "C" {
void hud_charge_meter_draw(double p1, double amount);
void hud_reticle_center_icon(void);
double hud_scale_screen_height(double value);
void hud_weapon_panel_draw(double p1, double p2, double p3, double p4, double p5,
                           double p6, double p7, double p8, uint c9, uint c10,
                           int spriteMain, int spriteOverlay, uint c13, int flag14,
                           uint c15, uint c16, float f17, float f18, float f19,
                           float f20, float f21);
int hud_weapon_slot_sprite(int slot);
}

// ============================================================================
// 0x801236f0 | hud_charge_meter_draw  (MASSIVE)
// Draws the charge/lock meter around the reticle. Two modes: when the meter's
// style word (slot[5]) is 0 it draws a simple two-half sprite bar (sprite 0x120);
// otherwise it draws an animated rotating fan of segments (sprite 0x11f) whose
// span, color, and pulse follow the charge 'amount' and a per-style color ramp.
// ============================================================================
extern "C" void hud_charge_meter_draw(double /*p1*/, double amount) {
    float* slot = (float*)FUN_803826e4();
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        float scale = *(float*)(&DAT_80578684 + *(int*)(DAT_80611d74 + 4) * 0x177c);
        double a = (double)FLOAT_80615718;
        if ((double)(float)(amount * (double)scale) < (double)FLOAT_80615718) {
            a = (double)(float)(amount * (double)scale);
        }
        FUN_803825c0((double)(float)((double)FLOAT_8061582c * a));

        if (slot[5] == 0.0f) {
            // simple two-half sprite bar (sprite 0x120)
            double halfW = (double)(slot[3] * FLOAT_8061082c);
            double cx = (double)slot[0];
            double halfH = (double)(slot[4] * FLOAT_80610830);
            double cy = (double)slot[1];
            double leftX = (double)(float)(cx - halfW);
            double topY = (double)(float)(cy - halfH);
            FUN_8016f00c(1);
            int tex = sprite_get_texture_id(0x120);
            int desc = *DAT_80610a20 + tex * 0x58;
            uint uvW = (uint)*(unsigned short*)(desc + 0x4e) << 4;
            uint uvH = (uint)*(unsigned short*)(desc + 0x52) << 4;
            FUN_8016e77c(tex);
            int cyI = (int)topY;
            uint botY = (uint)(cy + halfH);
            FUN_80170410((int)leftX, cyI, (int)*slot, botY, 0, 0, uvW, uvH);
            FUN_80170410((int)*slot, cyI, (int)(cx + halfW), botY, uvW, 0, 0, uvH);
        } else {
            // rotating fan of segments (sprite 0x11f)
            float local_bc, local_c8, local_d4, local_e0;  // x's
            float local_b8, local_c4, local_d0, local_dc;  // y's
            float local_b4, local_c0, local_cc, local_d8;  // z's
            uint color = 0;
            double span = (double)(float)((double)FLOAT_806157a8 * a);
            double baseX = (double)slot[2];
            FUN_8016f00c(2);
            float style = slot[5];
            double spanCur = (double)FLOAT_806157c4;
            double heightCur = (double)FLOAT_80615748;

            if (style == 4.2039e-45f) {   // style 3
                double amt = amount;
                if ((double)FLOAT_80615718 < amt) amt = (double)FLOAT_80615718;
                double clamped = amt;
                if ((double)FLOAT_80615734 < amt) clamped = (double)FLOAT_80615734;
                span = (double)(FLOAT_80615720 * (float)((double)FLOAT_80615748 * clamped));
                heightCur = (double)(FLOAT_80615748 *
                    (FLOAT_80615718 - (float)((double)FLOAT_80615748 * clamped)) + FLOAT_806158d0);
                if ((((double)FLOAT_806158f4 < amt) && (amt < (double)FLOAT_806158f8)) ||
                    (((double)FLOAT_8061571c < amt) && (amt < (double)FLOAT_806158fc)) ||
                    (((double)FLOAT_80615900 < amt) && (amt < (double)FLOAT_80615904))) {
                    span = (double)FLOAT_80615730;
                }
                spanCur = (double)(float)(spanCur +
                    (double)(FLOAT_80615748 * (float)((double)FLOAT_806157c4 * amt)));
            }

            float fill = slot[2];
            if (fill < FLOAT_8061574c) {
                if (fill < FLOAT_80615884) { FUN_80382730(); return; }
                span = (double)(float)(span * (double)((fill - FLOAT_80615884) / FLOAT_80615880));
            }
            int seg = FUN_803825c0((double)(float)((double)FLOAT_80615750 * span));
            if (style == 1.4013e-45f) {          // style 1
                color = seg << 0x18 | 0x286428;
            } else if (style == 2.8026e-45f) {   // style 2
                color = seg << 0x18 | 0xa2864;
            } else if (style == 4.2039e-45f) {   // style 3
                color = seg << 0x18 | 100;
            }

            float cx = *slot;
            uint ang = (uint)((double)FLOAT_806157c0 * spanCur + (double)FLOAT_80615734);
            float sinV = (float)((double)slot[4] * heightCur) * (&DAT_80583228)[ang & 0x7ff];
            float cosV = (float)((double)slot[3] * heightCur) *
                         *(float*)((int)&DAT_80583228 + ((ang + 0x200) * 4 & 0x1ffc));
            float sinW = sinV * FLOAT_8061082c;
            float cosW = cosV * FLOAT_8061082c;
            sinV = sinV * FLOAT_80610830;
            cosV = cosV * FLOAT_80610830;
            local_bc = cx + cosW;
            local_c8 = cx + sinW;
            local_d4 = cx - sinW;
            local_e0 = cx - cosW;
            float cy = slot[1];
            local_b8 = cy + sinV;
            local_c4 = cy - cosV;
            local_d0 = cy + cosV;
            local_dc = cy - sinV;
            local_b4 = (float)baseX;
            local_c0 = (float)baseX;
            local_cc = (float)baseX;
            local_d8 = (float)baseX;
            (void)local_b4; (void)local_c0; (void)local_cc; (void)local_d8;

            int tex = sprite_get_texture_id(0x11f);
            FUN_8016e77c(tex);
            int desc = *DAT_80610a20 + tex * 0x58;
            FUN_8017a62c((uint)*(unsigned short*)(desc + 0x4e), (uint)*(unsigned short*)(desc + 0x52),
                         color, &local_bc, &local_c8, &local_d4, &local_e0);
        }
    }
    FUN_80382730();
    return;
}

// ============================================================================
// 0x80123b34 | hud_reticle_center_icon
// Draws the pulsing center reticle icon (sprite 0x11e) for the active view. The
// icon pulses via the sine LUT in the idle case; when in the "targeting" state
// (DAT_80611d74+0xdf4 != 0) it offsets by the aim vector; when "locked"
// (DAT_80611d74+0xe10 != 0) it uses a faster blink. Sized square about the
// viewport center and colored by the per-view HUD alpha.
// ============================================================================
extern "C" void hud_reticle_center_icon(void) {
    FUN_803826e8();
    int view = *(int*)(DAT_80611d74 + 4);
    if (-1 < view) {
        double pulse = (double)FLOAT_80615964;
        uint span = 0xc;
        if (*(int*)(DAT_80611d74 + 0xe10) != 0) span = 10;
        int tex = sprite_get_texture_id(0x11e);
        uint cx = DAT_8057ff4c + *DAT_80611748 +
                  ((DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) >> 1);
        uint cy = DAT_8057ff50 + DAT_80611748[5] +
                  ((DAT_80611748[6] - DAT_8057ff58) - (DAT_8057ff50 + DAT_80611748[5]) >> 1);
        FUN_8016f00c(1);
        FUN_8016e77c(tex);

        if (*(int*)(DAT_80611d74 + 0xdf4) == 0) {
            double phaseD = (double)(CONCAT44(0x43300000, DAT_80611928 % 0x1e) ^ 0x80000000);
            pulse = (double)(FLOAT_806157a4 *
                (FLOAT_80615734 *
                 (&DAT_80583228)[(int)(FLOAT_806157c0 *
                    ((float)(phaseD - DOUBLE_80615738) / FLOAT_806157cc) + FLOAT_80615734) & 0x7ff] +
                 FLOAT_80615734) + FLOAT_8061582c);
        } else {
            double aimYD = (double)(CONCAT44(0x43300000,
                (int)(FLOAT_8061571c * (float)((double)CONCAT44(0x43300000, DAT_80611748[9] ^ 0x80000000) - DOUBLE_80615738))) ^ 0x80000000);
            cx = cx + (int)(*(float*)(DAT_80611d74 + 0xe04) *
                (float)((double)CONCAT44(0x43300000,
                    (int)(FLOAT_8061571c * (float)((double)CONCAT44(0x43300000, DAT_80611748[4] ^ 0x80000000) - DOUBLE_80615738)) ^ 0x80000000) - DOUBLE_80615738));
            cy = cy + (int)(*(float*)(DAT_80611d74 + 0xe08) * (float)(aimYD - DOUBLE_80615738));
        }
        if (*(int*)(DAT_80611d74 + 0xe10) != 0) {
            double blinkD = (double)(CONCAT44(0x43300000, DAT_80611928 % 0xf) ^ 0x80000000);
            pulse = (double)(FLOAT_80615968 * ((float)(blinkD - DOUBLE_80615738) / FLOAT_80615788) + FLOAT_806157c8);
        }

        double spanD = (double)CONCAT44(0x43300000, span ^ 0x80000000);
        double halfH = (double)((float)((double)CONCAT44(0x43300000, span ^ 0x80000000) - DOUBLE_80615738) * FLOAT_80610830);
        int desc = *DAT_80610a20;
        double halfW = (double)((float)(spanD - DOUBLE_80615738) * FLOAT_8061082c);
        FUN_803825c0((double)(float)((double)*(float*)(&DAT_80578684 + view * 0x177c) * pulse));
        desc = desc + tex * 0x58;
        FUN_80170410(
            (int)((double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738) - halfW),
            (int)((double)(float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738) - halfH),
            (int)((double)(float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738) + halfW),
            (int)((double)(float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738) + halfH),
            0, 0,
            (uint)*(unsigned short*)(desc + 0x4e) << 4, (uint)*(unsigned short*)(desc + 0x52) << 4);
    }
    FUN_80382734();
    return;
}

// ============================================================================
// 0x8012414c | hud_scale_screen_height
// Helper: scale 'value' by the viewport height (as a fraction), using a wider
// scale factor (FLOAT_80615748 = 2.0) when the split-screen layout is a specific
// stacked configuration (2-3 players, or the mapmaker preview flag). Used to size
// HUD elements relative to the current view's pixel height.
// ============================================================================
extern "C" double hud_scale_screen_height(double value) {
    float result;
    double heightD = (double)(float)((double)CONCAT44(0x43300000,
        (*(int*)(DAT_80611748 + 0x18) - DAT_8057ff58) -
        (DAT_8057ff50 + *(int*)(DAT_80611748 + 0x14)) ^ 0x80000000) - DOUBLE_80615738);

    // Wide (2x) scaling for the stacked 2-player split layout, or when the
    // active player has the "wide HUD" flag set in the mapmaker/3-player cases.
    int split = FUN_801c1714();
    bool wide;
    if (split == 2) {
        wide = true;
    } else if ((split == 3) && (DAT_80611d74 != 0) && (*(int*)(DAT_80611d74 + 4) == 0)) {
        wide = true;
    } else {
        wide = false;
    }

    if (wide &&
        (((DAT_805015bd != '\n') || (DAT_80612610 < 2)) ||
         ((DAT_80611d78 != 0) &&
          ((*(int*)(DAT_80611d78 + 0x1424) != 0) &&
           ((*(uint*)(*(int*)(DAT_80611d78 + 0x1424) + 0x21c) & 1) != 0))))) {
        result = FLOAT_80615748 * FLOAT_80615778 * (float)(value * heightD);
    } else {
        result = FLOAT_80615778 * (float)(value * heightD);
    }
    return (double)result;
}

// ============================================================================
// 0x8012429c | hud_weapon_panel_draw  (MASSIVE)
// Draws the weapon/status panel quad in the corner of the view: a base sprite
// (spriteMain) plus an optional overlay sprite (spriteOverlay), colored by the
// panel alpha (param_7) and a secondary ramp (param_8) that differs for the
// active vs inactive player. When flag14 is set and the view is active it also
// draws a downward selection arrow (sprite_draw_arrow_down). Uses a temporary
// scissor rect around the panel bounds.
// ============================================================================
extern "C" void hud_weapon_panel_draw(double /*p1*/, double p2, double p3, double p4, double p5,
                                      double p6, double p7, double p8, uint /*c9*/, uint /*c10*/,
                                      int spriteMain, int spriteOverlay, uint /*c13*/, int flag14,
                                      uint /*c15*/, uint /*c16*/, float f17, float /*f18*/,
                                      float /*f19*/, float /*f20*/, float /*f21*/) {
    int savedX, savedY, savedW, savedH;
    int rectX, rectY, rectW, rectH;

    unsigned long long push = FUN_803826c4();
    int activeFlag = (int)((unsigned long long)push >> 0x20);
    double arrowAmt = (double)f17;
    int view = *(int*)(DAT_80611d74 + 4);
    if (-1 < view) {
        rectW = 0x43300000;
        rectH = (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000;
        double panelW = (double)(FLOAT_80615778 *
            (float)((double)FLOAT_80615734 *
                    (double)(float)(p3 * (double)(float)((double)CONCAT44(0x43300000, rectH) - DOUBLE_80615738))));
        double scaledH = hud_scale_screen_height((double)(float)((double)FLOAT_80615734 * p4));
        double sw = (double)(float)(panelW * p5);

        uint topMag = DAT_8057ff50 + DAT_80611748[5] ^ 0x80000000;
        uint leftMag = DAT_8057ff4c + *DAT_80611748 ^ 0x80000000;
        uint spanX = (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000;
        uint spanY = (DAT_80611748[6] - DAT_8057ff58) - (DAT_8057ff50 + DAT_80611748[5]) ^ 0x80000000;
        double sh = (double)(float)(scaledH * p6);

        double cx = (double)(float)(panelW +
            (double)(FLOAT_80615778 *
                     (float)((double)(float)((double)CONCAT44(0x43300000, spanX) - DOUBLE_80615738)) +
                     (float)((double)CONCAT44(0x43300000, leftMag) - DOUBLE_80615738)));
        double cy = (double)(float)(scaledH +
            (double)(FLOAT_80615778 *
                     (float)(p2 * (double)(float)((double)CONCAT44(0x43300000, spanY) - DOUBLE_80615738)) +
                     (float)((double)CONCAT44(0x43300000, topMag) - DOUBLE_80615738)));

        double x0 = (double)(float)(cx - sw);
        double x1 = (double)(float)(cx + sw);
        double y0 = (double)(float)(cy - sh);
        double y1 = (double)(float)(cy + sh);

        // uv-space corner z's (unused decoration verts in original)
        double uvBias = (double)(FLOAT_80615778 *
            (float)((double)(float)((double)CONCAT44(0x43300000, spanY) - DOUBLE_80615738)) +
            (float)((double)CONCAT44(0x43300000, topMag) - DOUBLE_80615738));
        (void)uvBias;

        int texMain = sprite_get_texture_id(spriteMain);
        int desc = *DAT_80610a20 + texMain * 0x58;
        uint uvW = (uint)*(unsigned short*)(desc + 0x4e) << 4;
        uint uvH = (uint)*(unsigned short*)(desc + 0x52) << 4;

        uint cA = FUN_803825c0((double)(float)((double)FLOAT_806157ac * p7)) & 0xff | 0x14141400;
        uint cMain = FUN_803825c0((double)(float)((double)FLOAT_8061582c * p7)) & 0xff | 0x554b4100;
        if (activeFlag == 0) {
            if ((int)push != 0) {
                cMain = FUN_803825c0((double)(float)((double)FLOAT_8061582c * p7)) & 0xff | 0x551e1400;
            }
        } else {
            uint fanBase = FUN_803825c0((double)(float)((double)FLOAT_80615750 * p7));
            cA = fanBase & 0xff | 0x14141400;
            uint r, g, b;
            if ((int)push == 0) {
                r = FUN_803825c0((double)(float)((double)FLOAT_806158ec * p8 + (double)FLOAT_806158ec));
                g = FUN_803825c0((double)(float)((double)FLOAT_806157b0 * p8 + (double)FLOAT_806157c8));
                b = FUN_803825c0((double)(float)((double)FLOAT_80615788 * p8 + (double)FLOAT_8061594c));
            } else {
                r = FUN_803825c0((double)(float)((double)FLOAT_806158ec * p8 + (double)FLOAT_806158ec));
                g = FUN_803825c0((double)(float)((double)FLOAT_80615784 * p8 + (double)FLOAT_806157b0));
                b = FUN_803825c0((double)(float)((double)FLOAT_8061579c * p8 + (double)FLOAT_80615784));
            }
            cMain = (b & 0xff) << 8 | r << 0x18 | (g & 0xff) << 0x10 | fanBase;
        }

        FUN_8016e77c(texMain);
        FUN_8001d030(x0, y0, x1, y1, cx, cy, x1, y1, 0, 0, uvW, uvH, cA);

        if (spriteOverlay != -1) {
            FUN_8016f00c(2);
            int texOv = sprite_get_texture_id(spriteOverlay);
            FUN_8016e77c(texOv);
            FUN_8001d030(x0, y0, x1, y1, cx, cy, x1, y1, 0, 0, uvW, uvH, cMain);
            if ((double)FLOAT_80615730 < arrowAmt) {
                double one = (double)FLOAT_80615718;
                double aw = (double)(float)((double)(float)((double)FLOAT_80615880 * arrowAmt + one) * sw);
                double ah = (double)(float)((double)(float)((double)FLOAT_806157a8 * arrowAmt + one) * sh);
                FUN_803825c0((double)(FLOAT_806157cc * (float)(one - arrowAmt)));
                FUN_801708b4((double)(float)(cx - aw), (double)(float)(cy - ah),
                             (double)(float)(cx + aw), (double)(float)(cy + ah), 0, 0, uvW, uvH);
            }
            FUN_8016f00c(1);
        }

        FUN_8016f0a0(&savedX, &savedY, &savedW, &savedH);
        FUN_8016f0e4(DAT_8057ff4c + *DAT_80611748, DAT_8057ff50 + DAT_80611748[5],
                     DAT_80611748[1] - DAT_8057ff54, DAT_80611748[6] - DAT_8057ff58);
        if ((flag14 != 0) &&
            ((FLOAT_80615730 == *(float*)(&DAT_80579de4 + view * 0x177c)) || (activeFlag == 0))) {
            rectX = 0x43300000;
            rectY = (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000;
            (void)rectX;
            double arrowHalf = (double)(FLOAT_80615778 * FLOAT_8061585c *
                (float)((double)CONCAT44(0x43300000, rectY) - DOUBLE_80615738));
            sprite_draw_arrow_down(
                (double)(float)(cx - arrowHalf),
                -(double)(float)((double)FLOAT_8061596c * scaledH - y0),
                (double)(float)(cx + arrowHalf),
                -(double)(float)((double)FLOAT_80615970 * scaledH - y0));
        }
        FUN_8016f0e4(savedX, savedY, savedW, savedH);
    }
    FUN_80382710();
    return;
}

// ============================================================================
// 0x80124910 | hud_weapon_slot_sprite
// Maps a weapon slot index to the HUD reticle/icon sprite id to display. Returns
// -1 when the slot is empty, the slot is flagged hidden (DAT_8043e22c bit 0x10),
// or (for slot 1) the "no unarmed icon" player flag is set. Falls back to sprite
// 0x119 when the slot has no explicit sprite (-1) in DAT_8043d99c.
// ============================================================================
extern "C" int hud_weapon_slot_sprite(int slot) {
    int slotBase = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
    if (slot != 0) {
        if (*(char*)(slotBase + slot + 0x54c) != '\0') {
            int spr = DAT_8043d99c[slot * 5];
            if (((DAT_8043e22c[slot * 0xf] & 0x10) == 0) &&
                ((slot != 1) || (*(char*)(slotBase + 0x584) == '\0'))) {
                if (spr == -1) spr = 0x119;
                return spr;
            }
            return -1;
        }
    }
    return -1;
}
