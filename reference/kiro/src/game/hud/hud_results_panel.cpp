// Batch 341 | Date: 2026-09-04
// HUD results-panel dispatcher and its per-mode sub-renderers. This is the
// framed popup shown over the results screen; a game-mode switch picks which
// summary to draw (lives, winner icon, team score bar, award/player icon rows,
// team names) inside a 9-slice panel. hud_results_panel_update is the per-frame
// driver (fade in/out + layout + challenge/award overlays).
//
//   0x80120388 hud_results_render_lives_row      (per-player lives icons 0xdc/0xde)
//   0x80120720 hud_results_render_winner_icon    (winner/objective icon by mode)
//   0x801209e0 hud_results_render_team_bar        (team score tug-of-war bar 0xd9/0xda/0xea)
//   0x801212d8 hud_results_panel_dispatch         (mode -> sub-renderer + 9-slice panel)
//   0x80121b24 text_draw_char_outlined            (8-direction outline + center glyph)
//   0x80121c88 hud_results_render_award_title      (top award name, outlined, pulsing)
//   0x8012206c hud_results_render_challenge_times  (challenge best-time table)
//   0x801227ec hud_results_panel_update            (per-frame driver / fade / dispatch)
//
// Cross-file: hud_results_render_award_icons / _player_icons / _team_names and
// hud_results_mode_shows_stats live in hud_results_screen.cpp (batch 340).
//
// Subsystem: game (HUD rendering)

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// scalar float constants
extern float FLOAT_80615718;   // 1.0
extern float FLOAT_8061571c;
extern float FLOAT_8061572c;
extern float FLOAT_80615730;   // 0.0
extern float FLOAT_80615734;   // 0.5
extern float FLOAT_80615748;   // 2.0
extern float FLOAT_8061575c;
extern float FLOAT_80615760;
extern float FLOAT_806157b4;
extern float FLOAT_806157c0;
extern float FLOAT_806157c4;
extern float FLOAT_806157d4;
extern float FLOAT_806157dc;
extern float FLOAT_806157e4;
extern float FLOAT_806157ec;
extern float FLOAT_8061579c;
extern float FLOAT_80615750;
extern float FLOAT_8061576c;
extern float FLOAT_80615814;
extern float FLOAT_80615818;
extern float FLOAT_8061581c;
extern float FLOAT_80615838;
extern float FLOAT_80615884;
extern float FLOAT_8061588c;
extern float FLOAT_806158a4;
extern float FLOAT_8061594c;
extern float FLOAT_80615950;
extern float FLOAT_80615954;
extern float FLOAT_80615958;
extern float FLOAT_8061595c;
extern float FLOAT_80615960;
extern float FLOAT_80615868;
extern float FLOAT_80611674;
extern float FLOAT_806108e8;
extern float FLOAT_806108ec;
extern float FLOAT_8061082c;
extern float FLOAT_80610830;

// panel fade state
extern float FLOAT_80611fe4;
extern int   DAT_80611fe0;

extern float FLOAT_80611ff4;   // team-bar animated split ratio

extern double DOUBLE_80615738; // 0x4330..0 unsigned int->double bias
extern double DOUBLE_80615740; // signed int->double bias

// globals
extern uint  DAT_806109c0;      // render width
extern int*  DAT_80611748;      // viewport rect
extern int   DAT_80611928;      // wobble phase accumulator
extern int   DAT_80611904;      // current award/objective player index
extern int   DAT_80611910;      // current objective entity ptr
extern int   DAT_80611934;      // stats display mode
extern int   DAT_8061156c;      // suppression flag
extern int   DAT_80611d74;      // current player-view (int base; used as int)
extern int   DAT_80611d78;      // player-slot table base
extern int   DAT_80611d90;      // score record table base
extern int   DAT_80612098;      // tracked-player count
extern int   DAT_806118d4;      // objective timer numerator
extern int   DAT_806120b0;      // objective node count
extern int   DAT_806120d8;      // excluded-player mask
extern int   DAT_806120fc;      // extra score-record count
extern int   DAT_80612610;      // real-player count threshold
extern int   DAT_80612614;      // special/award mode id
extern int   DAT_805015d8;      // challenge id
extern uint  DAT_805015c0;      // feature flags
extern unsigned char DAT_805015bd; // game phase
extern unsigned char DAT_805015be; // game mode id
extern unsigned char DAT_80501573; // player count byte
extern int   DAT_80579de8;      // team-name record ptr
extern int   DAT_805014a0;      // award-slot player index table
extern int   DAT_80588888;      // objective node array
extern int   DAT_8057ff4c;      // layout left inset
extern int   DAT_8057ff50;      // layout top inset
extern int   DAT_8057ff54;      // layout right inset
extern int   DAT_8057ff58;      // layout bottom inset
extern int*  DAT_80610a20;      // texture descriptor table base
extern float DAT_80583228;      // sin/cos LUT
extern int   DAT_803f2c7c;      // per-team color table (5-word stride)
extern int   DAT_803f2c90;      // team-color base (5-word stride array head)
extern int   DAT_806108a0;      // column-format context

// helpers (raw)
extern int    FUN_801c1714(void);               // active player count
extern unsigned long long FUN_803826c4(void);   // HUD push (returns rect)
extern int    FUN_80382710(void);
extern unsigned long long FUN_803826e0(void);
extern int    FUN_8038272c(void);
extern unsigned long long FUN_803826b4(void);
extern int    FUN_80382700(void);
extern unsigned long long FUN_803826ec(void);
extern int    FUN_80382738(void);
extern unsigned long long FUN_803826dc(void);
extern int    FUN_80382728(void);
extern unsigned long long FUN_803826d0(void);
extern int    FUN_8038271c(void);
extern int    FUN_8016e8ac(int mode);
extern int    FUN_8016f00c(int mode);
extern int    FUN_8016e77c(int texId);
extern int    FUN_80170410(int, int, int, int, int, int, uint, uint);       // int-rect quad
extern int    FUN_80170b10(double, double, double, double, uint, int, uint, uint); // float quad w/ uv
extern int    FUN_801708b4(double, double, double, double, int, int, uint, uint);  // wobbled quad
extern int    FUN_8016f24c(int, int, int, int, int, int);   // panel bg fill
extern int    FUN_802e42c0(int, int, int, int, int);        // panel border
extern int    sprite_get_texture_id(int spriteId);
extern int    FUN_803825c0(double v);           // float -> clamped byte
extern uint   FUN_80132228(double t, uint colA, uint colB);  // color blend
extern uint   FUN_802764f4(int team);           // team objective-held test
extern uint   FUN_80276548(int team);           // team objective-secondary test
extern int    score_get_team(int team);
extern int    score_get_special(int rec);
extern int    FUN_802c7788(void);               // award -> sprite id
extern int    text_set_font_table(int table);
extern int    get_string_by_id(uint id);
extern double text_get_char_width(unsigned char* str);
extern double text_get_char_height(unsigned char* str);
extern int    text_apply_alpha(double a, unsigned char* str, int flags);
extern int    text_apply_alpha_fade(double a, unsigned char* str, int, int);
extern unsigned long long text_draw_char(double x, double y, unsigned char* str, uint color);
extern int    text_set_color_rgb(double r, double g, double b);
extern short* award_get_entry(int id);
extern unsigned char* FUN_8022a8a0(void);       // current challenge record ptr
extern unsigned char* FUN_80350540(double, double, double, double, double, double, double, double);
extern unsigned char* FUN_8022a8dc(double, double, long long, double, double,
                                   unsigned long long, unsigned long long, unsigned long long,
                                   int, int, uint, uint, uint, uint, int, uint);
extern unsigned char* FUN_8022b580(unsigned long long, double, long long, double, double,
                                   unsigned long long, unsigned long long, unsigned long long);
extern unsigned char* FUN_8012e878(unsigned long long, double, long long, double, double,
                                   unsigned long long, unsigned long long, unsigned long long,
                                   int*, int, int, uint, uint, uint, int, uint);
extern unsigned char* stats_get_challenge_record(int a, int id);
extern int    FUN_802c8b00(int a, int b, int c, uint d, uint e);
extern int    difficulty_get_name_string(int diff);
extern uint   color_fade_team_b(double t, int idx);
extern double screen_effect_get_damage_alpha(void);
extern void   hud_draw_panel_9slice(double x0, double y0, double x1, double y1, double alpha);
extern void   hud_slot_draw_secondary(void);
extern int    FUN_802f2fb4(int a);
extern int    FUN_8012810c(void);
extern int    FUN_801c2dcc(int mode);
extern bool   FUN_80130fc8(void);
extern int    FUN_802ef550(void);
extern uint   FUN_802ef5a8(int idx);
extern int    hud_results_mode_shows_stats(void);   // batch 340
extern void   hud_results_render_award_icons(double, int, int, int, int);   // batch 340
extern void   hud_results_render_player_icons(double, int, int, int, int);  // batch 340
extern void   hud_results_render_team_names(double, int, int, int, int);    // batch 340

// forward decls (this file)
extern "C" {
void hud_results_render_lives_row(int p1, int p2, int cellW);
void hud_results_render_winner_icon(void);
void hud_results_render_team_bar(int p1, int p2, int panelW, int panelH);
void hud_results_panel_dispatch(double alpha, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8);
void text_draw_char_outlined(double x, double y, unsigned char* str, uint centerCol, uint outlineCol);
void hud_results_render_award_title(void);
void hud_results_render_challenge_times(void);
}

// ============================================================================
// 0x80120388 | hud_results_render_lives_row
// Draws a centered row of "lives" icons (sprite 0xdc), one per player, using
// the team color slot; a live player's icon pulses (sin LUT via FUN_80132228)
// and eliminated players show a crossed-out overlay (sprite 0xde).
// ============================================================================
extern "C" void hud_results_render_lives_row(int p1, int p2, int cellW) {
    (void)p1; (void)p2;
    unsigned long long rect = FUN_803826c4();
    uint n = (uint)DAT_80501573;
    if (n < 3) n = 2;
    double d0 = 0.0;

    int tex = sprite_get_texture_id(0xdc);
    int cellSpan = cellW / (int)n - 8;
    int desc = *DAT_80610a20 + tex * 0x58;
    uint tw = (uint)*(unsigned short*)(desc + 0x4e);
    uint th = (uint)*(unsigned short*)(desc + 0x52);
    uint clamped = (0x26 < cellSpan) ? 0x26 : (uint)cellSpan;
    int halfW = (int)clamped >> 1;
    uint uvH = th << 4;
    uint uvW = tw << 4;
    float spacing = (float)((double)(longlong)(int)clamped - (double)0.0) * FLOAT_8061082c;
    int scaledH = (int)(FLOAT_80610830 *
                        (float)((double)(longlong)(int)(-(int)((float)((double)(longlong)(int)clamped - (double)0.0) *
                                     ((float)((double)(longlong)(int)th - (double)0.0) /
                                      (float)((double)(longlong)(int)tw - (double)0.0)))) - (double)0.0));
    int halfH = scaledH >> 1;
    int top = (int)rect + halfH;
    int bottom = (int)rect - halfH;
    int x = halfW + ((int)(rect >> 0x20) - (cellW >> 1)) + 2;
    int colOff = 0;

    for (int i = 0; i < (int)n; i++) {
        uint teamCol = *(uint*)((int)&DAT_803f2c90 + colOff);
        uint held = FUN_802764f4(i + 1);
        if (held == 0) {
            uint held2 = FUN_80276548(i + 1);
            FUN_803825c0((double)(float)((double)(float)((double)(longlong)(int)(teamCol & 0xff) - (double)0.0) * d0));
            int t2 = sprite_get_texture_id(0xdc);
            FUN_8016e77c(t2);
            FUN_80170410(x - halfW, bottom, x + halfW, top, 0, 0, uvW, uvH);
            if (held2 != 0) {
                int t3 = sprite_get_texture_id(0xde);
                FUN_8016e77c(t3);
                FUN_80170410(x - halfW, bottom, x + halfW, top, 0, 0, uvW, uvH);
            }
        } else {
            uint pulse = (&DAT_80583228)[(int)(FLOAT_806157c0 * FLOAT_80615818 *
                            (float)((double)(longlong)(int)DAT_80611928 - (double)0.0) +
                            FLOAT_80615734) & 0x7ff] != 0.0f ? 0 : 0;
            (void)pulse;
            uint blended = FUN_80132228((double)(FLOAT_80615734 *
                              (&DAT_80583228)[(int)(FLOAT_806157c0 * FLOAT_80615818 *
                                  (float)((double)(longlong)(int)DAT_80611928 - (double)0.0) +
                                  FLOAT_80615734) & 0x7ff] + FLOAT_80615734), teamCol, 0x7f7f7f80);
            FUN_803825c0((double)(float)((double)(float)((double)(longlong)(int)(blended & 0xff) - (double)0.0) * d0));
            int t2 = sprite_get_texture_id(0xdc);
            FUN_8016e77c(t2);
            FUN_80170410(x - halfW, bottom, x + halfW, top, 0, 0, uvW, uvH);
        }
        x = x + (cellW - ((int)spacing + 4)) / (int)(n - 1);
        colOff += 0x14;
    }
    FUN_80382710();
    return;
}

// ============================================================================
// 0x80120720 | hud_results_render_winner_icon
// Draws the single winner/objective icon for the current mode: the team that
// holds the objective (mode 0xb), the awarded player (mode 6), or the current
// weapon/target (mode 0x12). Aspect-correct centered on the panel.
// ============================================================================
extern "C" void hud_results_render_winner_icon(void) {
    unsigned long long rect = FUN_803826e0();
    int cx = (int)(rect >> 0x20);
    FUN_803825c0((double)(float)((double)FLOAT_80615750 * (double)0.0));
    int award = -1;

    if (DAT_805015be == 0xb) {
        int total = DAT_80612610 + DAT_806120fc;
        for (int i = 0; i < total; i++) {
            if ((DAT_806120d8 & 1 << i) != 0) { award = FUN_802c7788(); break; }
        }
    } else if (DAT_805015be < 0xb) {
        if ((DAT_805015be == 6) && (-1 < DAT_80611904) &&
            (DAT_80611904 < DAT_80612610 + DAT_806120fc)) {
            award = FUN_802c7788();
        }
    } else if ((DAT_805015be == 0x12) && (DAT_80611910 != 0) &&
               (*(int*)(DAT_80611910 + 0x20) == 8)) {
        award = FUN_802c7788();
    }

    if (award != -1) {
        int tex = sprite_get_texture_id(award);
        int desc = *DAT_80610a20 + tex * 0x58;
        uint tw = (uint)*(unsigned short*)(desc + 0x4e);
        uint th = (uint)*(unsigned short*)(desc + 0x52);
        float wpx = (float)(4503601774854176.0 - DOUBLE_80615738) * FLOAT_8061082c;  // fixed sprite width scale
        float hpx = FLOAT_80610830 *
                    (float)((double)(longlong)(int)(-(int)(FLOAT_8061575c *
                        ((float)((double)(longlong)(int)th - (double)0.0) /
                         (float)((double)(longlong)(int)tw - (double)0.0)))) - (double)0.0);
        FUN_8016e77c(tex);
        int halfH = (int)hpx >> 1;
        int halfW = (int)wpx >> 1;
        FUN_80170410(cx - halfW, (int)rect - halfH, cx + halfW, (int)rect + halfH,
                     0, 0, tw << 4, th << 4);
    }
    FUN_8038272c();
    return;
}

// ============================================================================
// 0x801209e0 | hud_results_render_team_bar
// Draws the two-team score "tug-of-war" bar: a fill quad split by the ratio of
// team1/(team1+team2) scores (animated toward the target via FLOAT_80611ff4),
// the per-objective-node segment icons (sprite 0xea), and the framed end caps
// (sprites 0xd9 / 0xda).
// ============================================================================
extern "C" void hud_results_render_team_bar(int p1, int p2, int panelW, int panelH) {
    (void)p1; (void)p2;
    unsigned long long rect = FUN_803826b4();
    int cy = (int)(rect >> 0x20);
    double d0 = 0.0;
    if (0 < (int)DAT_806120b0) {
        int savedBlend = FUN_8016e8ac(0);
        int savedMode = FUN_8016f00c(1);
        int texCapL = sprite_get_texture_id(0xd9);
        int texCapR = sprite_get_texture_id(0xda);
        int texSeg  = sprite_get_texture_id(0xea);
        int descBase4e = *DAT_80610a20 + 0x4e;
        int descBase52 = *DAT_80610a20 + 0x52;
        uint segTw = (uint)*(unsigned short*)(descBase4e + texSeg * 0x58);
        uint segTh = (uint)*(unsigned short*)(descBase52 + texSeg * 0x58);
        uint capTw = (uint)*(unsigned short*)(descBase4e + texCapL * 0x58);
        unsigned short capTh = *(unsigned short*)(descBase52 + texCapL * 0x58);

        uint s1 = score_get_team(1);
        int  s2 = score_get_team(2);
        FUN_803825c0((double)(float)((double)FLOAT_8061594c * d0));

        // target split ratio
        double ratio;
        bool team1Only = ((countLeadingZeros(s1) >> 5) != 0) && (s2 == 0);
        if (team1Only) {
            ratio = (double)FLOAT_80615734;
        } else if ((countLeadingZeros(s1) >> 5) == 0) {
            ratio = (s2 == 0) ? (double)FLOAT_80615718
                              : (double)((float)((double)(longlong)(int)s1 - (double)0.0) /
                                         (float)((double)(longlong)(int)((int)s1 + s2) - (double)0.0));
        } else {
            ratio = (double)FLOAT_80615730;
        }

        // panel-relative rectangle bounds (faithful bias math preserved)
        double left  = (double)(float)((double)(longlong)(int)((int)rect - (panelH >> 1)) - (double)0.0);
        double right = (double)(float)((double)(longlong)(int)((int)rect + (panelH >> 1)) - (double)0.0);
        double topY  = (double)(float)((double)(longlong)(int)((cy - (panelW >> 1)) + 2) - (double)0.0);
        double botY  = (double)(float)((double)(longlong)(int)((cy + (panelW >> 1)) + -2) - (double)0.0);
        double innerL = (double)(float)((double)FLOAT_80615814 * (double)(float)(right - left) + left);
        double innerR = (double)(float)((double)FLOAT_8061572c * (double)(float)(right - left) + left);
        double innerT = (double)(float)((double)FLOAT_806157d4 * (double)FLOAT_8061082c + topY);
        double innerB = -(double)(float)((double)FLOAT_8061581c * (double)FLOAT_8061082c - botY);

        // segment icons across the bar
        FUN_8016e77c(texSeg);
        double segX = innerT;
        double segW = (double)((float)((double)(longlong)(int)segTw - (double)0.0) * (float)((double)FLOAT_8061082c));
        double segH = (double)((float)((double)(longlong)(int)segTh - (double)0.0) * (float)((double)FLOAT_80610830));
        double segY = (double)(float)((double)FLOAT_80615734 * (double)(float)(innerL + innerR) -
                                      (double)(float)((double)FLOAT_80615734 * segH));
        FUN_803825c0((double)(float)((double)FLOAT_80615750 * d0));
        int nodeOff = 0;
        for (int i = 0; i < (int)DAT_806120b0; i++) {
            int owner = *(int*)(*(int*)(*(int*)((int)&DAT_80588888 + nodeOff) + 0x7c) + 0x1c);
            if (owner != 0) {
                FUN_803825c0((double)(float)(d0 * (double)(float)((double)(longlong)(int)((&DAT_803f2c7c)[owner * 5] & 0xff) - (double)0.0)));
            }
            FUN_80170410((int)segX, (int)segY, (int)(segX + segW), (int)(segY + segH),
                         segTw * 4, segTh * 4, segTw * 0xc, segTh * 0xc);
            segX = (double)(float)(segX + segW);
            nodeOff += 8;
        }

        // fill quad (animated split)
        FUN_8016e77c(texCapR);
        uint capUvW = capTw << 4;
        uint capUvH = (uint)capTh << 4;
        FLOAT_80611ff4 = (float)((double)(float)((double)FLOAT_80615718 /
                            (double)(float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) *
                            (double)(FLOAT_806108ec * (float)(ratio - (double)FLOAT_80611ff4)) +
                            (double)FLOAT_80611ff4);
        double animRatio = (double)FLOAT_80611ff4;
        if ((double)FLOAT_80615718 < animRatio) animRatio = (double)FLOAT_80615718;
        if (animRatio < (double)FLOAT_80615730) animRatio = (double)FLOAT_80615730;
        double splitX = (double)(float)((double)(float)(innerB - innerT) * animRatio + innerT);
        FUN_80170b10(splitX, (double)(float)((double)FLOAT_80615734 + left), innerB, innerR, capUvW, 0, capUvW, capUvH);
        FUN_80170b10(innerT, (double)(float)((double)FLOAT_80615734 + right), splitX, innerR, capUvW, 0, capUvW, capUvH);

        // end caps
        FUN_8016e77c(texCapL);
        double capW = (double)(FLOAT_8061082c * (float)((double)(longlong)(uint)capTw - (double)0.0));
        FUN_80170b10(innerT, left, (double)(float)(innerT + capW), innerL, 0, 0, capUvW, capUvH);
        FUN_80170b10(innerB, left, (double)(float)(innerB - capW), innerL, 0, 0, capUvW, capUvH);

        FUN_8016e8ac(savedBlend);
        FUN_8016f00c(savedMode);
    }
    FUN_80382700();
    return;
}

// ============================================================================
// 0x80121b24 | text_draw_char_outlined
// Draws a glyph string with a 1px/2px 8-direction outline (outlineCol) and a
// center pass (centerCol) on top. Used by the award title so it reads over any
// background.
// ============================================================================
extern "C" void text_draw_char_outlined(double x, double y, unsigned char* str,
                                        uint centerCol, uint outlineCol) {
    unsigned long long ctx = FUN_803826ec();
    unsigned char* s = (unsigned char*)(uintptr_t)(ctx >> 0x20);
    double two = (double)FLOAT_80615748;
    double one = (double)FLOAT_80615718;
    (void)str;
    // 2px diagonal ring (centerCol used as the mid pass color in the original)
    text_draw_char((double)(float)(x - two), (double)(float)(y - two), s, centerCol);
    double xp2 = (double)(float)(two + x);
    double yp2 = (double)(float)(two + y);
    text_draw_char(xp2, yp2, s, centerCol);
    text_draw_char(xp2, (double)(float)(y - two), s, centerCol);
    text_draw_char((double)(float)(x - two), yp2, s, centerCol);
    // 1px ring (outlineCol)
    text_draw_char((double)(float)(x - one), (double)(float)(y - one), s, outlineCol);
    double xp1 = (double)(float)(one + x);
    double yp1 = (double)(float)(one + y);
    text_draw_char(xp1, yp1, s, outlineCol);
    text_draw_char(xp1, (double)(float)(y - one), s, outlineCol);
    text_draw_char((double)(float)(x - one), yp1, s, outlineCol);
    // center glyph
    text_draw_char(x, y, s, (uint)ctx);
    FUN_80382738();
    return;
}

// ============================================================================
// 0x80121c88 | hud_results_render_award_title
// Draws the two-line award name (from award_get_entry(DAT_80612614)) near the
// bottom of the screen using text_draw_char_outlined, with a sin-LUT color
// pulse and a fade envelope keyed off the wobble phase.
// ============================================================================
extern "C" void hud_results_render_award_title(void) {
    FUN_803826dc();
    short* entry = award_get_entry(DAT_80612614);
    double fade = (double)(FLOAT_806157e4 *
                  (float)((double)FLOAT_80615718 / (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) *
                  (float)((double)(longlong)(int)DAT_80611928 - (double)0.0));
    if ((double)FLOAT_80615718 < fade) fade = (double)FLOAT_80615718;
    if (fade < (double)FLOAT_80615730) fade = (double)FLOAT_80615730;

    if ((entry != (short*)0) && (fade < (double)FLOAT_80615718)) {
        unsigned char* line0 = (unsigned char*)get_string_by_id((int)entry[0]);
        unsigned char* line1 = (unsigned char*)get_string_by_id((int)entry[1]);
        int savedBlend = FUN_8016e8ac(0);
        int savedMode = FUN_8016f00c(1);
        int savedFont = text_set_font_table(6);
        double h1 = text_get_char_height(line1);
        double h0 = text_get_char_height(line0);
        uint bottomLimit = DAT_80611748[6] - DAT_8057ff58;
        double baseY = (double)(float)((double)(float)((double)(longlong)(int)(DAT_80611748[6] - 0x46) - (double)0.0) + h1);
        double x = (double)(float)((double)(longlong)(int)(DAT_8057ff4c + *DAT_80611748) - (double)0.0);
        if ((double)(float)((double)(longlong)(int)bottomLimit - (double)0.0) < baseY) {
            baseY = (double)(float)((double)(longlong)(int)bottomLimit - (double)0.0);
        }
        // pulsing color from the sin LUT
        double half = (double)FLOAT_80615734;
        double amp = (double)(float)((double)FLOAT_80615748 * fade);
        float pulse = (float)(half *
            (&DAT_80583228)[(int)((double)FLOAT_806157c0 *
                (double)(FLOAT_806157c4 * (float)(amp - half)) + half) & 0x7ff] + half);
        if (half <= fade) {
            amp = (double)(float)((double)FLOAT_80615748 * (double)(float)((double)FLOAT_80615718 - fade));
        }
        float pc = pulse; if (FLOAT_80615718 < pc) pc = FLOAT_80615718; if (pc < FLOAT_80615730) pc = FLOAT_80615730;
        uint centerCol = (int)(FLOAT_80615750 * pc) & 0xff | 0x7f7f7f00;
        uint outCol = (int)(FLOAT_80615750 * pc) & 0xff;
        double ampC = amp; if ((double)FLOAT_80615718 < ampC) ampC = (double)FLOAT_80615718; if (ampC < (double)FLOAT_80615730) ampC = (double)FLOAT_80615730;
        uint midCol = (int)((double)FLOAT_80615750 * ampC) & 0xff;

        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            baseY = (double)(float)((double)(longlong)(int)bottomLimit - (double)0.0);
        }
        double y1 = (double)(float)(baseY - h1);
        text_draw_char_outlined(x, (double)((float)(y1 - h0) - FLOAT_8061579c), line0, centerCol, outCol);
        text_draw_char_outlined(x, y1, line1, centerCol, midCol);
        FUN_8016e8ac(savedBlend);
        FUN_8016f00c(savedMode);
        text_set_font_table(savedFont);
    }
    FUN_80382728();
    return;
}

// ============================================================================
// 0x8012206c | hud_results_render_challenge_times
// Draws the challenge best-time panel: a framed background, the challenge name,
// then a table of difficulty rows (difficulty name + your time vs the target
// time from stats_get_challenge_record), each faded in by a per-frame envelope.
// ============================================================================
extern "C" void hud_results_render_challenge_times(void) {
    FUN_803826d0();
    short* rec = (short*)FUN_8022a8a0();
    double one = (double)FLOAT_80615718;
    double fade = (double)(FLOAT_806157e4 *
                  (float)(one / (double)(float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) *
                  (float)((double)(longlong)(int)DAT_80611928 - (double)0.0));
    if (one < fade) fade = one;
    if (fade < (double)FLOAT_80615730) fade = (double)FLOAT_80615730;

    if ((rec != (short*)0) && (fade < (double)FLOAT_80615718)) {
        int savedBlend = FUN_8016e8ac(0);
        int savedMode = FUN_8016f00c(1);
        int savedFont = text_set_font_table(6);
        int left = DAT_8057ff4c + *DAT_80611748;
        int width = (DAT_80611748[1] - DAT_8057ff54) - left;
        uint centerX = left + (width >> 1);
        double d0 = 0.0;

        float halfBar = FLOAT_80615960 * (float)((double)(longlong)(int)width - (double)0.0);
        double panelL = (double)((float)((double)(longlong)(int)centerX - (double)0.0) - halfBar);
        double panelR = (double)((float)((double)(longlong)(int)centerX - (double)0.0) + halfBar);
        double panelTop = (double)(float)((double)(longlong)(int)(DAT_80611748[6] - DAT_8057ff58) - (double)0.0);
        double panelBot = panelTop;
        unsigned char* name = (unsigned char*)get_string_by_id((int)rec[0]);

        // fade envelope (triangle over the pulse)
        double env;
        if ((double)FLOAT_80615734 <= fade) {
            float f = FLOAT_80615814;
            if ((float)((double)FLOAT_80615718 - fade) < FLOAT_80615814) f = (float)((double)FLOAT_80615718 - fade);
            env = -(double)(FLOAT_80615748 * f - FLOAT_80615718);
        } else {
            double t = (double)FLOAT_80615814; if (fade < t) t = fade;
            env = (double)(float)((double)FLOAT_80615748 * t);
        }
        float envF = (float)env; if (FLOAT_80615718 < envF) envF = FLOAT_80615718; if (envF < FLOAT_80615730) envF = FLOAT_80615730;
        uint titleCol = (int)(FLOAT_80615750 * envF) & 0xff | 0x64646400;

        // framed panel background + border
        text_set_font_table(6);
        double titleH = (double)(float)(panelTop - (double)FLOAT_80611674);
        text_set_font_table(0x13);
        double pad = (double)FLOAT_8061576c;
        double bx0 = panelL - pad;
        double by0 = -(double)(float)(pad * (double)FLOAT_80611674 - titleH);
        double bx1 = pad + panelR;
        double by1 = by0 - pad;
        FUN_8016f24c(8, (int)bx0, (int)by1, (int)bx1, (int)(pad + panelBot), 0);
        FUN_802e42c0((int)bx0, (int)by1, (int)bx1, (int)(pad + panelBot), 0x14);

        // challenge name
        text_set_font_table(6);
        text_apply_alpha_fade((double)(float)(panelR - panelL), name, 0, 1);
        text_draw_char(panelL, by1, name, titleCol);
        double nameH = text_get_char_height(name);
        double rowY = (double)(float)(by1 + nameH);

        // "Best" header + a fixed set of difficulty rows
        text_set_font_table(0x13);
        int challengeId = DAT_805015d8;
        double rowStep = (double)(FLOAT_8061082c * (float)((double)FLOAT_80611674 / (double)FLOAT_80610830));
        double col2X = (double)((float)((double)FLOAT_80615814 * rowStep) + (float)(panelL + rowStep));
        unsigned char* best = (unsigned char*)get_string_by_id(0x3d1);
        text_apply_alpha_fade(env, best, 0, 1);
        text_draw_char(col2X, rowY, best, titleCol);

        int* chal = (int*)stats_get_challenge_record(0, challengeId);
        unsigned char* target;
        if (*chal == 0) {
            target = (unsigned char*)get_string_by_id(0x3d2);
        } else {
            target = FUN_8022a8dc(d0, rowY, (long long)(int)rowY, d0, d0, 0, 0, 0,
                                  challengeId, challengeId, 1, 0, 0, 0, left, 0);
        }
        text_apply_alpha_fade(env, target, 0, 1);
        double tw = text_get_char_width(target);
        text_draw_char((double)(float)(panelR - tw), rowY, target, titleCol);

        for (int diff = 3; 0 < diff; diff--) {
            rowY = (double)(float)(rowY + (double)FLOAT_80611674);
            int rowI = (int)rowY;
            FUN_802c8b00(diff, (int)panelL, rowI, (uint)rowStep, (uint)(double)FLOAT_80611674);
            int diffName = difficulty_get_name_string(diff);
            unsigned char* dstr = FUN_8012e878(0, rowY, (long long)rowI, d0, d0, 0, 0, 0,
                                               &DAT_806108a0, diffName, rowI, (uint)rowStep,
                                               (uint)(double)FLOAT_80611674, 0, left, 0);
            text_apply_alpha_fade(env, dstr, 0, 1);
            text_draw_char(col2X, rowY, dstr, titleCol);
            unsigned char* tstr = FUN_8022b580((unsigned long long)(uintptr_t)dstr, rowY, (long long)rowI, d0, d0, 0, 0, 0);
            text_apply_alpha_fade(env, tstr, 0, 1);
            uint col = color_fade_team_b(env, diff);
            double twd = text_get_char_width(tstr);
            text_draw_char((double)(float)(panelR - twd), rowY, tstr, col);
        }
        text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718, (double)FLOAT_80615718);
        FUN_8016e8ac(savedBlend);
        FUN_8016f00c(savedMode);
        text_set_font_table(savedFont);
    }
    FUN_8038271c();
    return;
}

// ============================================================================
// 0x801212d8 | hud_results_panel_dispatch
// Selects the per-mode results sub-renderer (via a function pointer), measures
// the content, draws the 9-slice panel background behind it, then invokes the
// chosen renderer. Modes: 5 lives, 6/0xb/0x12 winner icon, 7/9 award icons,
// 0xc player icons, 0xf team bar, 0x10 team names.
// ============================================================================
extern "C" void hud_results_panel_dispatch(double alpha, unsigned long long p2, unsigned long long p3,
                                           unsigned long long p4, unsigned long long p5,
                                           unsigned long long p6, unsigned long long p7,
                                           unsigned long long p8) {
    typedef void (*ResultsRenderer)(double, int, int, int, int);
    ResultsRenderer fn = (ResultsRenderer)0;
    int rec = DAT_80579de8;
    int contentW = 0, contentH = 0;
    double d0 = 0.0;
    (void)p2; (void)p3; (void)p4; (void)p5; (void)p6; (void)p7; (void)p8;

    float baseScale = (FUN_801c1714() == 1) ? FLOAT_80615950 : FLOAT_806158a4;
    double scale = (double)baseScale;

    switch (DAT_805015be) {
        case 5:
            fn = (ResultsRenderer)&hud_results_render_lives_row;
            contentW = (int)(FLOAT_806157b4 * FLOAT_8061082c);
            contentH = (int)(FLOAT_80615958 * FLOAT_80610830);
            break;
        case 6: case 0xb: case 0x12:
            fn = (ResultsRenderer)&hud_results_render_winner_icon;
            contentW = (int)(FLOAT_806157ec * FLOAT_8061082c);
            contentH = (int)(FLOAT_8061575c * FLOAT_80610830);
            break;
        case 7:
        case 9: {
            int count = 0, off = 0;
            for (int i = 0; i < DAT_80612098; i++) {
                if ((-1 < *(int*)((int)&DAT_805014a0 + off)) &&
                    (score_get_special(DAT_80611d90 + *(int*)((int)&DAT_805014a0 + off) * 0xea0) != 0)) {
                    count++;
                }
                off += 4;
            }
            contentW = (int)(scale * (double)FLOAT_8061082c);
            contentH = (int)(FLOAT_80615954 * FLOAT_80610830);
            int cap = (FUN_801c1714() == 1) ? 10 : 0x14;
            if (count <= cap >> 1) contentH = contentH >> 1;
            fn = (ResultsRenderer)&hud_results_render_award_icons;
            break;
        }
        case 0xc: {
            int count = 0, off = 0;
            int total = DAT_80612610 + DAT_806120fc;
            for (int i = 0; i < total; i++) {
                if ((DAT_806120d8 & 1 << *(int*)(DAT_80611d90 + off + 8)) == 0) count++;
                off += 0xea0;
            }
            contentW = (int)(scale * (double)FLOAT_8061082c);
            contentH = (int)(FLOAT_80615954 * FLOAT_80610830);
            int cap = (FUN_801c1714() == 1) ? 10 : 0x14;
            if (count <= cap >> 1) contentH = contentH >> 1;
            fn = (ResultsRenderer)&hud_results_render_player_icons;
            break;
        }
        case 0xf:
            fn = (ResultsRenderer)&hud_results_render_team_bar;
            contentW = (int)(FLOAT_8061595c * FLOAT_8061082c);
            contentH = (int)(FLOAT_8061575c * FLOAT_80610830);
            break;
        case 0x10: {
            // team-name banner: measure one or two team name strings
            int owner = 0; bool twoTeams = false;
            int i = FUN_801c1714();
            while (i = i + -1, -1 < i) {
                int slot = DAT_80611d78 + i * 0x141c;
                int t = *(int*)(*(int*)(*(int*)(slot + 0x14) + 0x7c) + 0x84);
                if ((owner != 0) && (owner != t)) { twoTeams = true; break; }
                owner = t;
            }
            if ((rec != 0) && (0 < owner)) {
                if (twoTeams) {
                    unsigned char* a = (unsigned char*)get_string_by_id((uint)*(unsigned short*)(rec + 2));
                    unsigned char* b = (unsigned char*)get_string_by_id((uint)*(unsigned short*)(rec + 4));
                    int prevFont = text_set_font_table(0x13);
                    if (DAT_80612614 == 100) {
                        a = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
                        b = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
                    }
                    text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), a, 0);
                    int wa = (int)((double)FLOAT_80615760 * (double)FLOAT_8061082c + text_get_char_width(a));
                    int ha = (int)text_get_char_height(a);
                    text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), b, 0);
                    int wb = (int)((double)FLOAT_80615760 * (double)FLOAT_8061082c + text_get_char_width(b));
                    int hb = (int)text_get_char_height(b);
                    text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718, (double)FLOAT_80615718);
                    text_set_font_table(prevFont);
                    if (wb < wa) wb = wa;
                    contentW = wb << 1;
                    contentH = (hb < ha) ? ha : hb;
                } else {
                    unsigned short sid = (owner == 1) ? *(unsigned short*)(rec + 2) : *(unsigned short*)(rec + 4);
                    unsigned char* nm = (unsigned char*)get_string_by_id((uint)sid);
                    int prevFont = text_set_font_table(0x13);
                    if (DAT_80612614 == 100) nm = (unsigned char*)FUN_80350540(d0, d0, d0, d0, d0, d0, d0, d0);
                    text_apply_alpha((double)(FLOAT_8061588c * FLOAT_8061082c), nm, 0);
                    contentW = (int)((double)FLOAT_80615760 * (double)FLOAT_8061082c + text_get_char_width(nm));
                    contentH = (int)text_get_char_height(nm);
                    text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718, (double)FLOAT_80615718);
                    text_set_font_table(prevFont);
                }
                fn = (ResultsRenderer)&hud_results_render_team_names;
            }
            break;
        }
    }

    if (fn == (ResultsRenderer)0) {
        DAT_80611fe0 = 0;
        FLOAT_80611fe4 = FLOAT_80615730;
    } else {
        uint halfH = contentH >> 1;
        int left = DAT_8057ff4c + *DAT_80611748;
        int right = DAT_80611748[1] - DAT_8057ff54;
        DAT_80611fe0 = contentH;
        int cy;
        if (FUN_801c1714() == 1) {
            cy = DAT_8057ff50 + DAT_80611748[5] + halfH;
        } else {
            cy = DAT_8057ff50 + DAT_80611748[5] +
                 ((DAT_80611748[6] - DAT_8057ff58) - (DAT_8057ff50 + DAT_80611748[5]) >> 1);
        }
        if (FUN_801c1714() == 1) {
            alpha = (double)(float)(alpha * screen_effect_get_damage_alpha());
        }
        int cx = DAT_8057ff4c + *DAT_80611748 +
                 ((DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) >> 1);

        // 9-slice panel behind the content
        double px0 = (double)((float)((double)(longlong)(int)cy - (double)0.0) -
                              (float)((double)(longlong)(uint)halfH - (double)0.0) -
                              FLOAT_8061576c * FLOAT_80610830);
        double px1 = (double)((float)((double)(longlong)(int)cy - (double)0.0) +
                              (float)((double)(longlong)(uint)halfH - (double)0.0) +
                              FLOAT_8061576c * FLOAT_80610830);
        double py0 = (double)((float)((double)(longlong)(int)(cx - (contentW >> 1)) - (double)0.0) -
                              FLOAT_8061576c * FLOAT_8061082c);
        double py1 = (double)((float)((double)(longlong)(int)((contentW >> 1) + cx) - (double)0.0) +
                              FLOAT_8061576c * FLOAT_8061082c);
        FUN_8016f00c(1);
        hud_draw_panel_9slice(py0, px0, py1, px1, alpha);
        FUN_8016f00c(1);
        fn(alpha, left + (right - left >> 1), cy, contentW, contentH);
    }
    FUN_8038272c();
    return;
}

// ============================================================================
// 0x801227ec | hud_results_panel_update
// Per-frame driver for the results panel. Recomputes layout insets, animates
// the panel fade (FLOAT_80611fe4) in/out based on the game phase, calls
// hud_results_panel_dispatch when visible, and layers the challenge-time
// (hud_results_render_challenge_times) / award-title
// (hud_results_render_award_title) overlays. Called from the HUD sub-dispatcher
// FUN_801c29d0.
// ============================================================================
extern "C" void hud_results_panel_update(void) {
    double d0 = 0.0;

    // early-out for certain special modes
    if (DAT_80612614 != 0x69) {
        if (DAT_80612614 < 0x69) {
            if (100 < DAT_80612614) { FLOAT_80611fe4 = FLOAT_80615730; return; }
        } else if (DAT_80612614 < 0x6c) {
            FLOAT_80611fe4 = FLOAT_80615730; return;
        }
    }

    // recompute layout insets from the viewport
    uint insetA = (*(int*)(DAT_80611748 + 0x34) != 0) ? 10 : 0x12;
    DAT_8057ff4c = (int)(FLOAT_806157dc * *(float*)(DAT_80611748 + 0x28));
    DAT_8057ff50 = (int)((double)FLOAT_80610830 *
                         (double)(float)((double)(longlong)(uint)insetA - (double)0.0));
    DAT_8057ff54 = (int)(FLOAT_806157dc * *(float*)(DAT_80611748 + 0x28));
    uint insetB = (*(int*)(DAT_80611748 + 0x34) != 0) ? 10 : 0x12;
    DAT_8057ff58 = (int)((double)FLOAT_80610830 *
                         (double)(float)((double)(longlong)(uint)insetB - (double)0.0));

    // detect whether any player is in a state that suppresses the panel
    bool suppressed = false;
    int i = 0;
    while (true) {
        if (FUN_801c1714() <= i) break;
        if (FUN_802ef5a8(i) != 0) { suppressed = true; break; }
        i++;
    }

    if (hud_results_mode_shows_stats() == 0) {
        FLOAT_80611fe4 = FLOAT_80615730;
    } else if ((DAT_80611934 != 7) || suppressed) {
        if ((double)FLOAT_80615730 < (double)FLOAT_80611fe4) {
            // fade out
            FLOAT_80611fe4 = -(float)((double)FLOAT_80615748 *
                (double)(float)((double)FLOAT_806108e8 *
                    (double)(FLOAT_80615718 / (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0))) -
                (double)FLOAT_80611fe4);
        }
    } else {
        if ((double)FLOAT_80611fe4 < (double)FLOAT_80615718) {
            // fade in
            FLOAT_80611fe4 = (float)((double)FLOAT_80615748 *
                (double)(FLOAT_806108ec *
                    (float)((double)FLOAT_80615718 / (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0))) +
                (double)FLOAT_80611fe4);
        }
    }

    // clamp the fade to [0,1]
    double f = (double)FLOAT_80611fe4;
    double clamped = (f > 0.0) ? f : 0.0;
    if (clamped > (double)FLOAT_80615718) clamped = (f > 0.0) ? f : 0.0;  // (preserve original min/clamp order)
    if (clamped > 1.0) clamped = 1.0;
    FLOAT_80611fe4 = (float)clamped;
    if (suppressed) FLOAT_80611fe4 = FLOAT_80615730;

    if ((double)FLOAT_80615730 < (double)FLOAT_80611fe4) {
        hud_results_panel_dispatch((double)FLOAT_80611fe4, 0, 0, 0, 0, 0, 0, 0);
    }

    // optional challenge-medal callback
    if (((DAT_805015c0 & 0x40) != 0) && (DAT_805015d8 == 0x13) && (DAT_80611d74 != 0) &&
        (FUN_802f2fb4(*(int*)(DAT_80611d74 + 4)) == 0)) {
        FUN_8012810c();
    }

    // secondary HUD slots when the results panel isn't in an exclusive mode
    if ((FUN_801c2dcc(DAT_80612614) == 0) && (DAT_80611d74 != 0) && !FUN_80130fc8()) {
        hud_slot_draw_secondary();
    }

    // award title (phase 0x0a) or challenge times overlay
    if ((DAT_805015bd == '\n') && (DAT_8061156c == 0) && (FUN_802ef550() == 0)) {
        hud_results_render_award_title();
    } else if (((DAT_805015c0 & 0x8000000) != 0) && (DAT_8061156c == 0) && (FUN_802ef550() == 0)) {
        hud_results_render_challenge_times();
    }
    (void)d0;
    return;
}
