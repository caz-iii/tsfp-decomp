// Address block: 0x80132510 - 0x801332e4 | Batch: batch_359 | Date: 2026-09-04
//
// Loading-screen subsystem: the rotating "loading tip" randomizer/formatter and
// the loading-screen renderer (progress bars, background wipe, tip text).
//
// Tip IDs run 0x162f..0x1646. The 24-entry tip list is initialised sequentially
// then Fisher-Yates shuffled; loadtip_pick_next walks the shuffled list and
// reshuffles when exhausted. loadtip_format switches on the tip ID to build the
// display string (game stats, random level/gamemode names, completion %, etc).
//
// Non-matching decomp: behaviour preserved, register-pair returns modelled as
// void, raw symbols declared as per-file externs matching their call sites.

#include "common.h"

// ---- Data tables / state (raw) --------------------------------------------
extern float DAT_80585228;   // 15-float progress-bar accumulator array
extern float FLOAT_8061206c; // bar reset scalar A
extern float FLOAT_80612068; // bar reset scalar B
extern int   DAT_80585264;   // 24-entry (0x18) tip ID list
extern int   DAT_8061205c;   // current index into shuffled tip list
extern int   DAT_8061090c;   // loading-screen initialised flag
extern int   DAT_80612374;   // loader ready flag
extern int   DAT_80612050;   // "error" state flag
extern int   DAT_80612054;   // "abort/message" state flag
extern int   DAT_80612020;   // animation frame counter
extern int   DAT_80612058;   // active loading slot index
extern float FLOAT_80612048; // begin timer
extern float FLOAT_80612060; // scroll x accumulator
extern float FLOAT_80612064; // begin elapsed
extern float FLOAT_80612070; // fade factor
extern float DAT_80583228;   // sine LUT (from random.cpp sine_table_build)

// Pointers to engine globals used by the renderer.
extern int  *DAT_80610a20;   // loading-slot table base + counts
extern int   DAT_806109c4;   // screen width
extern int   DAT_806109c8;   // screen height
extern int   DAT_806109c0;   // screen width (alt)
extern int  *DAT_80611748;   // viewport rect
extern int   DAT_80612254;   // cached slot pointer
extern void *DAT_805852c4;   // formatted-tip string buffer
extern void *DAT_80610910;   // format token "%s"
extern void *DAT_8061091c;   // format token (two-line message)
extern void *DAT_80610914;   // format token (percentage)
extern float FLOAT_806108e8; // scroll speed
extern float FLOAT_80611a64, FLOAT_80611a68, FLOAT_80611a6c, FLOAT_80611a70; // layout metrics

// ---- Float / double constants (raw) ---------------------------------------
extern float  FLOAT_80615ba8, FLOAT_80615bac, FLOAT_80615bb0, FLOAT_80615bb4;
extern float  FLOAT_80615bb8, FLOAT_80615bbc, FLOAT_80615bc0, FLOAT_80615bd0;
extern float  FLOAT_80615bd4, FLOAT_80615bd8, FLOAT_80615bdc, FLOAT_80615be0;
extern float  FLOAT_80615be4, FLOAT_80615be8, FLOAT_80615bec, FLOAT_80615c00;
extern float  FLOAT_80615c04, FLOAT_80615c08, FLOAT_80615c0c, FLOAT_80615c10;
extern float  FLOAT_80615c14, FLOAT_80615c18, FLOAT_80615c1c, FLOAT_80615c20;
extern float  FLOAT_80615c24, FLOAT_80615c28, FLOAT_80615c2c, FLOAT_80615c30;
extern double DOUBLE_80615bc8, DOUBLE_80615bf0, DOUBLE_80615bf8, DOUBLE_80615c38;

// ---- Raw helper functions (declared to match call sites) -------------------
extern int    FUN_803825c0(double x);        // float -> clamped byte
extern void   FUN_803826f0(void);            // math scratch push
extern void   FUN_8038273c(void);            // math scratch pop
extern double FUN_803826e4(void);            // read a timing/scalar value
extern void   FUN_80382730(void);            // render scratch pop
extern double game_frand(void);

extern int  get_string_by_id(int id);
extern void format_string_varargs(double a1, ...); // varargs string formatter
extern int  debug_log_formatted(double a1, ...);
extern int  level_get_info_by_id(int id);
extern int  gamemode_find_in_table(int id);
extern int  stats_compute_player_completion(void);
extern void FUN_8038b510(int *dst, int *src);   // string copy
extern void FUN_80387fb8(double a1, ...);        // formatter variant
extern int  FUN_802c72b8(void);
extern int  FUN_802c72fc(int idx);
extern int  FUN_802dd86c(void);

// Renderer primitives.
extern void  FUN_8001cac0(double a, int slot);
extern void  FUN_8016f0e4(int x, int y, int w, int h);
extern void  FUN_8016f00c(int mode);
extern void  FUN_8016ee98(void);
extern int   FUN_8016e490(int a);
extern void  FUN_8016e8ac(int a);
void gx_bind_material_fallback(void);
extern void  FUN_801706cc(double a, double b, double c, double d, int e, int f, int g, int h);
extern void  FUN_8018ec78(void);
extern void  FUN_8016e77c(int slot);
extern unsigned long long FUN_80170410(int a, int b, int c, int d, int e, int f, uint g, uint h);
extern void  FUN_8016f4bc(int x, int y, int w, int h, uint color);
extern int   sprite_get_texture_id(int id);
extern int   text_string_to_upper(int str);
extern void  text_set_font_table(int table);
extern double text_get_char_height(void *str);
extern double text_get_char_width(void *str);
extern void  text_draw_char(double x, double y, void *str, uint color);
extern void  text_draw_char_drop_shadow(double x, double y, void *str, uint color, int shadow);
extern void *text_check_scale(double scale, void *buf);
extern void  text_get_dimensions_int(int *wOut, int *hOut, void *str);

// Cross-references within this file.
uint color_scale_lo(double factor, uint color); // defined in color_ops.cpp
void loadtip_shuffle(void);
void loadtip_format(void);
void loadtip_pick_next(int forceReshuffle);

// ---------------------------------------------------------------------------
// loadtip_reset_bars().  Address: 0x80132510
// Resets the 15-float progress-bar accumulator array and two related scalars
// to a constant.
// ---------------------------------------------------------------------------
void loadtip_reset_bars(void)
{
    float v = FLOAT_80615ba8;
    FLOAT_8061206c = v;
    FLOAT_80612068 = v;
    float *bars = &DAT_80585228;
    for (int i = 0; i < 15; ++i) {
        bars[i] = v;
    }
}

// ---------------------------------------------------------------------------
// loadtip_init_id_list().  Address: 0x80132540
// Fills the 24-entry tip ID list with sequential IDs starting at 0x162f.
// ---------------------------------------------------------------------------
void loadtip_init_id_list(void)
{
    int *ids = &DAT_80585264;
    for (int i = 0; i < 0x18; ++i) {
        ids[i] = i + 0x162f;
    }
}

// ---------------------------------------------------------------------------
// loadtip_shuffle().  Address: 0x80132570
// Fisher-Yates shuffle of the 24-entry tip ID list using game_frand().
// ---------------------------------------------------------------------------
void loadtip_shuffle(void)
{
    FUN_803826f0();
    double span = (double)FLOAT_80615bb0;
    double bias = (double)FLOAT_80615bac;
    int *ids = &DAT_80585264;
    for (int i = 0; i < 0x18; ++i) {
        double r = game_frand();
        int j = (int)(span * r + bias);
        int tmp = ids[i];
        ids[i] = ids[j];
        ids[j] = tmp;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// loadtip_pick_next().  Address: 0x8013260c
// Selects the next tip from the shuffled list (reshuffling when the list is
// exhausted or forceReshuffle is set), then formats it.
// ---------------------------------------------------------------------------
void loadtip_pick_next(int forceReshuffle)
{
    int *ids = &DAT_80585264;
    if ((DAT_8061205c == 0) || (forceReshuffle != 0)) {
        loadtip_shuffle();
    }
    // Format the current tip. The formatter reads the selected ID internally.
    loadtip_format();
    DAT_8061205c = (DAT_8061205c + 1) % 0x18;
}

// ---------------------------------------------------------------------------
// loadtip_format().  Address: 0x80132678
// Builds the display string for the current tip ID (0x162f..0x1646). Depending
// on the tip it inserts random level/gamemode names, kill/objective stats, the
// player's completion percentage, etc. Writes into DAT_805852c4.
// ---------------------------------------------------------------------------
void loadtip_format(void)
{
    int *ids = &DAT_80585264;
    int tipId = ids[DAT_8061205c];

    switch (tipId) {
    case 0x162f: {
        // Random unlocked-cheat style tip: pick a valid entry in a 0x20-stride
        // table (DAT_803f2dcc), skipping empty/reserved slots.
        int pick = 0x13;
        for (int tries = 0; tries < 0x32; ++tries) {
            double r = game_frand();
            int idx = (int)((double)FLOAT_80615bb4 * r + (double)FLOAT_80615bac) + 1;
            extern int DAT_803f2dcc; extern short DAT_803f2de4;
            if (((&DAT_803f2dcc)[idx * 8] != 0) && (idx != 2) && (idx != 0x11)) { pick = idx; break; }
            pick = 0x13;
        }
        extern short DAT_803f2de4;
        int arg = get_string_by_id((int)(&DAT_803f2de4)[pick * 0x10]);
        int fmt = get_string_by_id(tipId);
        format_string_varargs(0.0, &DAT_805852c4, fmt, &DAT_80610910, arg);
        break;
    }
    case 0x1630: case 0x1633: case 0x163d: case 0x163f:
        loadtip_pick_next(1);
        break;
    case 0x1631: case 0x1632: case 0x1637: case 0x163e:
    case 0x1640: case 0x1641: case 0x1642: case 0x1643:
    case 0x1644: case 0x1645: case 0x1646: {
        int *src = (int *)(uintptr_t)get_string_by_id(tipId);
        FUN_8038b510((int *)&DAT_805852c4, src);
        break;
    }
    case 0x1634: {
        int val = FUN_802c72b8();
        char *fmt = (char *)(uintptr_t)get_string_by_id(tipId);
        FUN_80387fb8(0.0, fmt, val);
        break;
    }
    case 0x1635: {
        // Count non-empty entries in the 0x20-stride table (18 entries).
        extern int DAT_803f2dcc;
        int count = 0;
        for (int i = 0, off = 0x40; i < 0x12; ++i, off += 0x20) {
            if (*(int *)((char *)&DAT_803f2dcc + off) != 0) count++;
        }
        char *fmt = (char *)(uintptr_t)get_string_by_id(tipId);
        FUN_80387fb8(0.0, fmt, count);
        break;
    }
    case 0x1636: {
        // Random level name from a valid, selectable level id.
        int pick = 0x30;
        for (int tries = 0; tries < 0x32; ++tries) {
            double r = game_frand();
            int idx = (int)((double)FLOAT_80615bb8 * r + (double)FLOAT_80615bac) + 1;
            int info = level_get_info_by_id(idx);
            uint *flags = (uint *)(uintptr_t)level_get_info_by_id(idx);
            if ((info != 0) && ((*flags & 0x10) != 0) &&
                (gamemode_find_in_table(idx) != -1)) { pick = idx; break; }
            pick = 0x30;
        }
        extern int DAT_803f31dc;
        int slot = gamemode_find_in_table(pick);
        int arg = get_string_by_id((&DAT_803f31dc)[slot * 0xf]);
        int fmt = get_string_by_id(tipId);
        format_string_varargs(0.0, &DAT_805852c4, fmt, &DAT_80610910, arg);
        break;
    }
    case 0x1638: {
        double completion = (double)stats_compute_player_completion();
        if ((double)FLOAT_80615bbc < completion) {
            loadtip_pick_next(1);
        } else {
            int pct = debug_log_formatted(completion, &DAT_80610914);
            int fmt = get_string_by_id(tipId);
            format_string_varargs(0.0, &DAT_805852c4, fmt, &DAT_80610910, pct);
        }
        break;
    }
    case 0x1639: {
        extern int DAT_803f31dc;
        double r = game_frand();
        int idx = (int)((double)FLOAT_80615bc0 * r + (double)FLOAT_80615bac);
        int arg = get_string_by_id((&DAT_803f31dc)[idx * 0xf]);
        int fmt = get_string_by_id(tipId);
        format_string_varargs(0.0, &DAT_805852c4, fmt, &DAT_80610910, arg);
        break;
    }
    case 0x163a: {
        int val = FUN_802dd86c();
        char *fmt = (char *)(uintptr_t)get_string_by_id(tipId);
        FUN_80387fb8(0.0, fmt, val);
        break;
    }
    case 0x163b: {
        int count = FUN_802c72b8();
        double span = (double)(float)((double)(CONCAT44(0x43300000, count - 1) ^ 0x80000000) - DOUBLE_80615bc8);
        double r = game_frand();
        int val = FUN_802c72fc((int)(r * span + (double)FLOAT_80615bac));
        int arg = get_string_by_id(val);
        int fmt = get_string_by_id(tipId);
        format_string_varargs(0.0, &DAT_805852c4, fmt, &DAT_80610910, arg);
        break;
    }
    case 0x163c:
        loadtip_pick_next(1);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// loadscreen_render().  Address: 0x80132a50
// Renders the loading screen for the active slot: background wipe, progress bar
// grid (with per-column fade driven by the sine LUT), the rotating tip text,
// and (in error/abort states) a centred two-line message. progressA/progressB
// (param_2/param_3) drive the two bar fills.
// ---------------------------------------------------------------------------
void loadscreen_render(unsigned long long /*r3*/, double progressA, double progressB,
                       unsigned long long p4, unsigned long long p5, unsigned long long p6,
                       unsigned long long p7, unsigned long long p8)
{
    double t = (double)FUN_803826e4();
    int slot = DAT_80612058;
    double mA = (double)FLOAT_80611a64;
    double mB = (double)FLOAT_80611a6c;
    double barTop = (double)(float)((double)FLOAT_80615bc0 * mA + mB);
    double barBottom = (double)(float)((double)FLOAT_80615bd0 * mA + mB);

    bool normalState = (DAT_80612050 == 0) && (DAT_80612054 == 0);
    if (normalState) {
        sprite_get_texture_id(0);
    }

    int *slotRec = (int *)(uintptr_t)(*DAT_80610a20 + slot * 0x58);
    uint texH = (uint)*(unsigned short *)((char *)slotRec + 0x52);
    double texHmagic = (double)CONCAT44(0x43300000, texH);
    uint texHscaled = (texH << 4);
    uint texWscaled = (uint)*(unsigned short *)((char *)slotRec + 0x4e) << 4;

    FUN_8001cac0((double)(float)(texHmagic - DOUBLE_80615c38), slot);
    uint bgAlpha = (uint)FUN_803825c0((double)(float)((double)FLOAT_80615bd4 * t));
    for (int spin = 0xf; spin > 1; --spin) { /* fixed spin delay */ }

    if (normalState) {
        double half = (double)FLOAT_80615bd8;
        double phaseIdxF = (double)(CONCAT44(0x43300000, DAT_80612020 % 0x208) ^ 0x80000000);
        uint lutIdx = (uint)((double)FLOAT_80615bdc *
                    (double)(FLOAT_80615be0 * FLOAT_80615be4 *
                             ((float)(phaseIdxF - DOUBLE_80615bc8) / FLOAT_80615be8))
                    + (double)FLOAT_80615bac);
        double sinePulse = (double)(float)((double)FLOAT_80615bac *
                    (double)(float)(half + (double)(float)(&DAT_80583228)[lutIdx & 0x7ff]) + half);
        if (half < sinePulse) sinePulse = half;

        FUN_8016f0e4(0, 0, DAT_806109c4, DAT_806109c8);
        FUN_8016f00c(1);
        FUN_8016ee98();
        int saved = FUN_8016e490(-0x7fe7082c);
        FLOAT_80612070 = (float)((double)FLOAT_80615bec *
                    (double)(float)((double)FLOAT_80615bd8 - progressA) + (double)FLOAT_80615bd8);
        FUN_8016ee98();
        FUN_8016e8ac(0);
        FUN_8016f00c(0xe);
        if ((slot < 0) || (DAT_80610a20[3] <= slot) || (slot <= DAT_80610a20[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *DAT_80610a20 + slot * 0x58;
        }
        FUN_801706cc(DOUBLE_80615bf0, DOUBLE_80615bf0, DOUBLE_80615bf8, DOUBLE_80615bf8,
                     *DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
        FUN_8016e490(saved);
        FUN_8018ec78();
        FUN_8016f00c(1);
        DAT_80612020 = DAT_80612020 + 1;

        int wipeH = (int)(FLOAT_80615bd4 * (float)((double)FLOAT_80615c00 - sinePulse));
        if (0 < wipeH) {
            FUN_8016e77c(slot);
            FUN_80170410(0, (int)barTop, DAT_806109c4, (int)barBottom, 0, 0, texWscaled, texHscaled);
        }
        FUN_8016f00c(1);
        FUN_8016f4bc(0, 0, DAT_806109c4, (int)barTop, bgAlpha & 0xff);

        // Title text (uppercased).
        int titleStr = get_string_by_id(0xdf2);
        void *upper = (void *)(uintptr_t)text_string_to_upper(titleStr);
        double titleX = (double)(FLOAT_80615c04 * FLOAT_80611a68 + FLOAT_80611a70);
        text_set_font_table(9);
        double titleH = text_get_char_height(upper);
        text_draw_char(titleX, (double)(float)(-(double)(float)((double)FLOAT_80615c08 *
                        (double)FLOAT_80611a64 - barTop) - titleH), upper, 0x6464647f);

        FUN_8016f4bc(0, (int)barBottom, DAT_806109c4, DAT_806109c8, bgAlpha & 0xff);

        // Scrolling tip text.
        double tipX = (double)FLOAT_80612060;
        double tipH = text_get_char_height(&DAT_805852c4);
        double tipY = (double)(float)(-(double)(float)((double)FLOAT_80615c0c *
                        (double)FLOAT_80611a64 - barBottom) + tipH);
        text_set_font_table(4);
        text_draw_char(tipX, tipY, &DAT_805852c4, 0x3c3c3c7f);

        // Advance / wrap the tip scroll.
        double wF = (double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000);
        double hF = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000);
        FLOAT_80612060 = -((float)(wF - DOUBLE_80615bc8) * FLOAT_80615c10 * FLOAT_806108e8 *
                    (FLOAT_80615bd8 / (float)(hF - DOUBLE_80615bc8)) - FLOAT_80612060);
        double tipW = text_get_char_width(&DAT_805852c4);
        if ((float)((double)FLOAT_80612060 + tipW) < FLOAT_80615ba8) {
            double wrapF = (double)(CONCAT44(0x43300000,
                        (int)(FLOAT_80615c14 * (float)((double)CONCAT44(0x43300000,
                                DAT_806109c4 ^ 0x80000000) - DOUBLE_80615bc8))) ^ 0x80000000);
            FLOAT_80612060 = (float)(wrapF - DOUBLE_80615bc8);
        }

        // Progress-bar column grid: per-column fade accumulation from a table.
        double colTop = (double)(FLOAT_80615c08 * FLOAT_80611a64 + FLOAT_80611a6c);
        double gridBottom = barBottom - colTop;
        double gridScale = (double)FLOAT_80615c20;
        double gridLeft = (double)(FLOAT_80615c18 * FLOAT_80611a68 + FLOAT_80611a70);
        double gridSpan = (double)(float)((double)(FLOAT_80615c1c * FLOAT_80611a68 + FLOAT_80611a70) - gridLeft);

        float *bars = &DAT_80585228;
        for (int col = 0; col < 0xf; ++col) {
            float capped = FLOAT_80615bd8;
            double colF = (double)(CONCAT44(0x43300000, col) ^ 0x80000000);
            if ((double)((float)(colF - DOUBLE_80615bc8) / FLOAT_80615bc0) < progressA) {
                double wF2 = (double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000);
                bars[col] = FLOAT_80615c24 * FLOAT_806108e8 *
                            (FLOAT_80615bd8 / (float)(wF2 - DOUBLE_80615bc8)) + bars[col];
                if (capped < bars[col]) bars[col] = capped;
            }
        }

        int rowY = (int)((double)(float)gridBottom - colTop);
        double dx0 = (double)(FLOAT_80615c28 * (float)(gridSpan * gridScale));
        double dx1 = (double)(FLOAT_80615c2c * (float)(gridSpan * gridScale));
        for (int col = 0; col < 0x1e; ++col) {
            double colF = (double)(CONCAT44(0x43300000, col) ^ 0x80000000);
            double x0 = (double)(float)(dx0 + (double)(float)(gridSpan *
                        (double)(float)((double)(float)(colF - DOUBLE_80615bc8) * gridScale) + gridLeft));
            double x1 = (double)(float)(x0 + dx1);
            int mirror = (0xe < col) ? (0x1d - col) : col;
            uint colColor = color_scale_lo(
                        (double)(float)((double)FLOAT_80615bd8 - (double)(float)bars[mirror]), 0x50);
            if ((double)(float)bars[mirror] < (double)FLOAT_80615bd8) {
                FUN_8016f4bc((int)x0, rowY, (int)x1, (int)gridBottom, colColor);
            }
        }
    } else if (DAT_80612054 != 0) {
        // Abort/message state: centred two-line message.
        FUN_8016e77c(slot);
        unsigned long long ctx = FUN_80170410(0, 0, DAT_806109c4, DAT_806109c8, 0, 0, texWscaled, texHscaled);
        int line2 = get_string_by_id(0xdf4);
        int line1 = get_string_by_id(0xdf3);
        FUN_80387fb8((double)ctx, (int)(uintptr_t)&DAT_805852c4, &DAT_8061091c, line1, line2);
        text_set_font_table(0xf);
        void *scaled = text_check_scale((double)FLOAT_80615c30, &DAT_805852c4);
        int msgW, msgH;
        text_get_dimensions_int(&msgW, &msgH, scaled);
        int vx = DAT_80611748[2];
        int vy = DAT_80611748[6];
        FUN_8016f00c(1);
        double cx = (double)CONCAT44(0x43300000, (vx - ((msgW >> 1) + (int)(msgW < 0 && (msgW & 1)))) ^ 0x80000000);
        double cy = (double)CONCAT44(0x43300000, ((vy - msgH) - 0x30) ^ 0x80000000);
        text_draw_char_drop_shadow((double)(float)(cx - DOUBLE_80615bc8),
                                   (double)(float)(cy - DOUBLE_80615bc8), scaled, 0x6e6e6e7f, 0x7f);
    }

    // Foreground overlay pass.
    FUN_8016f0e4(0, 0, DAT_806109c4, DAT_806109c8);
    FUN_8016e8ac(1);
    FUN_8016f00c(1);
    FUN_8016ee98();
    uint fgAlpha = (uint)FUN_803825c0((double)(float)((double)FLOAT_80615bd4 * progressB));
    if ((DAT_80612050 != 0) || (DAT_80612054 != 0)) {
        barTop = (double)FLOAT_80615ba8;
    }
    FUN_8016f4bc(0, (int)barTop, DAT_806109c4, DAT_806109c8, fgAlpha & 0xff);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// loadscreen_begin().  Address: 0x801332e4
// First-frame loading-screen setup: kicks off the loader (FUN_80133414(10)),
// waits until content is ready (or an error occurs), then runs the post-load
// step. Called once from outer_game_loop.
// ---------------------------------------------------------------------------
void loadscreen_begin(void)
{
    extern void FUN_80133414(int a);
    extern void FUN_80133ae8(void);

    if (DAT_8061090c == 0) {
        FUN_80133414(10);
        FLOAT_80612048 = FLOAT_80615bd8;
        do {
            if ((DAT_80612050 != 0) || (FLOAT_80615bd8 <= FLOAT_80612064)) break;
        } while (DAT_80612374 == 0);
        if (DAT_8061090c == 0) {
            FUN_80133ae8();
        }
        DAT_8061090c = 1;
    }
}
// ===========================================================================
// Address block: 0x80133358 - 0x8013383c | Batch: batch_360 | Date: 2026-09-04
//
// Loading-screen control / lifecycle layer (surrounds the renderer above).
//
// These functions drive the per-frame loading-screen state machine that
// outer_game_loop pumps: setup (pack + background texture load + tip pick),
// per-frame progress integration + render, the animated progress bar advance,
// and the small setter/getter accessors for phase, layout mode and bounds.
//
// Non-matching decomp: behaviour preserved. Register-pair f1 pass-through into
// the varargs asset/formatter helpers is modelled as a plain double argument
// (0.0), matching loadtip_format's existing convention. Raw symbols are
// declared as per-file externs matching their call sites; symbols already
// declared at the top of this file are reused.
// ---------------------------------------------------------------------------

// ---- Additional state / config globals (raw) ------------------------------
extern char  DAT_80501524;   // level/gamemode selector char ('g','j','f','d','k','e','8'...)
extern char  DAT_80501525;   // sub-mode selector char ('\n' == story)
extern uint  DAT_80501528;   // global game-state flag bits
extern int   DAT_80610908;   // current loading phase index (0..0xA)
// DAT_806109c0 already declared at top of file (screen width alt / timing scalar source).
extern int   DAT_806108f8, DAT_806108fc, DAT_80610900, DAT_80610904; // bounds rect
extern float FLOAT_806108e8; // scroll speed (reused)
extern int   DAT_806108e0;   // "setup done" flag
extern float FLOAT_80612060; // scroll x accumulator (reused)
extern float FLOAT_80612064; // begin elapsed (reused)
extern float FLOAT_80612048; // begin timer (reused)
extern int   DAT_80612050;   // error-state flag (reused)
extern int   DAT_80612054;   // abort/message-state flag (reused)
extern int   DAT_80612058;   // active loading slot / fallback texture id (reused)
extern int   DAT_8061204c;   // layout mode (reused)
extern int   DAT_80612614;   // tip/frame counter used to derive DAT_80612050/54

// Level/gamemode + award lookup tables and cursors.
extern int   DAT_805cc624, DAT_805cc628, DAT_805cc630, DAT_805cc634;
extern int   DAT_80612be8;   // level metadata handle
extern char  DAT_803f2930;   // 0x44-stride table: background texture id at entry
extern char  DAT_803f31ec;   // 0x3c-stride table: background texture id at entry
extern char  DAT_80471c54;   // 0x24-stride award-id table

// Progress-bar timing constants (FLOAT_80615ba8/bd8/c14 already declared at top).
extern float FLOAT_80615bd4;                 // 255.0f alpha scale

// Element tables addressed off small negative displacements (data in .sdata2).
extern char  DAT_8040d788;   // 0x2c-stride phase keyframe table (per layout mode)
extern char  DAT_8040d80c;   // rate divisor within the same keyframe table
extern char  DAT_8040d890;   // per-layout progress divisor
extern char  DAT_8040d89c;   // per-layout progress threshold
extern int   DAT_805831a8;   // draw-list sentinels
extern int   DAT_805853c4;   // draw-list buffer
extern int   DAT_805855c8, DAT_805855cc, DAT_80611d54; // draw-list head pointers
extern char  s_pak_loadbar_pak_8040d908[];              // "pak/loadbar.pak"
extern char  s_assets_ts_textures_gamecube__s_g_8040d918[]; // "assets/ts/textures/gamecube/%s.g..."

// ---- Raw helper functions (declared to match call sites) -------------------
extern int  FUN_80133aa0(unsigned long long r3, double f1, double f2,
                         unsigned long long p4, unsigned long long p5,
                         unsigned long long p6, unsigned long long p7,
                         unsigned long long p8); // loading render dispatch
extern void FUN_80133ae8(void);                  // post-load step
extern void FUN_80133414(int phase);             // loadscreen_set_phase (defined below)
extern void FUN_801333cc(int mode);              // loadscreen_set_layout_mode (defined below)
extern void FUN_801333d4(double v);              // loadscreen_advance_progress (defined below)
extern int  FUN_8013369c(void);                  // loadscreen_select_background_texture (below)

extern void FUN_8016f0e4(int x, int y, int w, int h);
extern void FUN_8016e8ac(int a);
extern void FUN_8016f00c(int mode);
extern void FUN_8016ee98(void);
extern void FUN_8016f24c(int a, int b, int c, int w, int h, int f);
extern void FUN_8001c8ec(void);
extern void sprite_set_virtual_screen_from_rect(double x0, double y0, double w, double h);
extern int  sprite_get_texture_id(int id);       // (declared above too; harmless single decl here)

// Pack / asset loading.
extern int  pack_open(unsigned long long r3, double f1, double f2, unsigned long long p4,
                      unsigned long long p5, unsigned long long p6, unsigned long long p7,
                      unsigned long long p8, char *name, int a, int b, uint story,
                      uint r7, uint r8, uint r9, uint r10);
extern void pack_destroy_slot(void);
extern int  asset_load_from_dvd(unsigned long long r3, double f1, double f2, unsigned long long p4,
                                unsigned long long p5, unsigned long long p6, unsigned long long p7,
                                unsigned long long p8, void *buf, int a, uint *b, uint story,
                                uint r7, uint r8, uint r9, uint r10);
extern int  asset_get_size(unsigned long long r3, double f1, double f2, unsigned long long p4,
                           unsigned long long p5, unsigned long long p6, unsigned long long p7,
                           unsigned long long p8, void *buf);
extern unsigned long long FUN_80387fb8(unsigned long long r3, double f1, double f2,
                                       unsigned long long p4, unsigned long long p5,
                                       unsigned long long p6, unsigned long long p7,
                                       unsigned long long p8, int buf, char *fmt, void *arg,
                                       uint story, uint r7, uint r8, uint r9, uint r10);
extern void FUN_803994d0(int handle, int size);
extern void FUN_8017bffc(unsigned long long r3, double f1, double f2, unsigned long long p4,
                         unsigned long long p5, unsigned long long p6, unsigned long long p7,
                         unsigned long long p8, int handle, int a, int *out, uint story,
                         uint r7, uint r8, uint r9, uint r10);
extern void *sprite_get_texture_name(int id);

// Game-mode / award lookups used by the background-texture selector.
extern char *FUN_8022d9a4(int idx);              // controller record -> texture id
extern int   FUN_80253df0(int a);
extern int   award_find_by_id(int id);
extern int  *FUN_80339b90(int handle, int magic); // level metadata lookup
extern char *FUN_8031d468(uint idx);              // sublevel record -> texture id

// ---------------------------------------------------------------------------
// loadscreen_render_frame_if_active().  Address: 0x80133358
// Per-frame entry from the game loop: unless we are in a special mode ('g','j'
// or flag 0x100000), if the loading screen is still initialised it invokes the
// render dispatch, then clears the initialised flag so it renders exactly once.
// ---------------------------------------------------------------------------
void loadscreen_render_frame_if_active(unsigned long long r3, double f1, double f2,
                                       unsigned long long p4, unsigned long long p5,
                                       unsigned long long p6, unsigned long long p7,
                                       unsigned long long p8)
{
    if (((DAT_80501524 != 'g') && (DAT_80501524 != 'j')) && ((DAT_80501528 & 0x100000) == 0)) {
        if (DAT_8061090c != 0) {
            FUN_80133aa0(r3, f1, f2, p4, p5, p6, p7, p8);
        }
        DAT_8061090c = 0;
    }
}

// ---------------------------------------------------------------------------
// loadscreen_is_active().  Address: 0x801333bc
// Returns 1 while the loading screen is initialised (DAT_8061090c != 0), else
// 0. Original uses the count-leading-zeros >> 5 idiom for the boolean.
// ---------------------------------------------------------------------------
uint loadscreen_is_active(void)
{
    return countLeadingZeros((uint)DAT_8061090c) >> 5;
}

// ---------------------------------------------------------------------------
// loadscreen_set_layout_mode().  Address: 0x801333cc
// Selects the progress-bar keyframe layout (index into the 0x2c-stride tables).
// ---------------------------------------------------------------------------
void loadscreen_set_layout_mode(int mode)
{
    DAT_8061204c = mode;
}

// ---------------------------------------------------------------------------
// loadscreen_advance_progress().  Address: 0x801333d4
// Monotonic clamp of the begin timer FLOAT_80612048 into [current, 1.0].
// New progress can only move forward and never past 1.0.
// ---------------------------------------------------------------------------
void loadscreen_advance_progress(double v)
{
    if ((double)FLOAT_80615bd8 < v) {
        v = (double)FLOAT_80615bd8;
    }
    if (v < (double)FLOAT_80612048) {
        v = (double)FLOAT_80612048;
    }
    FLOAT_80612048 = (float)v;
}

// ---------------------------------------------------------------------------
// loadscreen_set_bounds().  Address: 0x80133400
// Stores the loading-screen render rectangle (x, y, w, h).
// ---------------------------------------------------------------------------
void loadscreen_set_bounds(int x, int y, int w, int h)
{
    DAT_806108f8 = x;
    DAT_806108fc = y;
    DAT_80610900 = w;
    DAT_80610904 = h;
}

// ---------------------------------------------------------------------------
// loadscreen_set_phase().  Address: 0x80133414
// Sets the current loading phase index and resets the begin timer to 0.
// ---------------------------------------------------------------------------
void loadscreen_set_phase(int phase)
{
    DAT_80610908 = phase;
    FLOAT_80612048 = FLOAT_80615ba8;
}

// ---------------------------------------------------------------------------
// loadscreen_tick().  Address: 0x80133424
// Per-frame progress integration + render. Interpolates the phase keyframe pair
// to produce the current progress fraction, advances the elapsed accumulator
// once the phase threshold is crossed, feeds loadscreen_advance_progress, and
// (on the first render frame, DAT_8061090c == 0) sets up the virtual screen and
// calls loadscreen_render.
// ---------------------------------------------------------------------------
void loadscreen_tick(void)
{
    const int mode = DAT_8061204c;
    const int phase = DAT_80610908;

    // Keyframe delta for the current phase within this layout mode's 0x2c-stride
    // table (base at -0x7fbf2874 == 0x8040d78c; low base at &DAT_8040d788).
    float phaseDelta = FLOAT_80615ba8;
    if (phase + 1 < 0xb) {
        phaseDelta =
            *(float *)((char *)0 + mode * 0x2c + phase * 4 + -0x7fbf2874) -
            *(float *)(&DAT_8040d788 + mode * 0x2c + phase * 4);
    }

    double begin = (double)FLOAT_80612048;
    double phaseFrac =
        (double)(float)(begin * (double)phaseDelta +
                        (double)*(float *)(&DAT_8040d788 + mode * 0x2c + phase * 4));

    // Once the frame time crosses this layout's threshold, advance the elapsed
    // accumulator by dt / rate.
    if ((double)*(float *)(&DAT_8040d89c + mode * 4) <= phaseFrac) {
        double dt = (double)(FLOAT_806108e8 *
                    (FLOAT_80615bd8 /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615bc8)));
        FLOAT_80612064 = FLOAT_80612064 + (float)(dt / (double)*(float *)(&DAT_8040d890 + mode * 4));
    }

    // Clamp the elapsed value for the wipe amount (unused result kept faithful).
    double elapsed = (double)FLOAT_80612064;
    double clampedHi = elapsed;
    if ((double)FLOAT_80615bd8 < elapsed) clampedHi = (double)FLOAT_80615bd8;
    if ((double)FLOAT_80615ba8 <= clampedHi) {
        if ((double)FLOAT_80615bd8 < elapsed) elapsed = (double)FLOAT_80615bd8;
    } else {
        elapsed = (double)FLOAT_80615ba8;
    }

    // If this phase advances the progress bar, push the new fraction forward.
    float rate = *(float *)(&DAT_8040d80c + mode * 0x2c + phase * 4);
    if (FLOAT_80615ba8 < rate) {
        double dt = (double)(FLOAT_806108e8 *
                    (FLOAT_80615bd8 /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615bc8)));
        FUN_801333d4((double)(float)(dt * (double)(FLOAT_80615bd8 / rate) + begin));
    }

    // First render frame: build the virtual screen and paint the loading UI.
    if (DAT_8061090c == 0) {
        FUN_8016f0e4(0, 0, DAT_806109c4, DAT_806109c8);
        FUN_8016e8ac(1);
        FUN_8016f00c(0xe);
        FUN_8016ee98();
        FUN_8016f24c(0x1f, 0, 0, DAT_806109c4, DAT_806109c8, 0);

        double h = (double)(float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80615bc8);
        double w = (double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615bc8);
        sprite_set_virtual_screen_from_rect((double)FLOAT_80615ba8, (double)FLOAT_80615ba8, w, h);
        FUN_8001c8ec();

        if (((DAT_80612050 != 0) || (sprite_get_texture_id(0) >= 0)) && (DAT_80501524 != '\0')) {
            loadscreen_render((unsigned long long)0, phaseFrac, elapsed, 0, 0, 0, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// loadscreen_select_background_texture().  Address: 0x8013369c
// Resolves the loading-screen background texture id from the current game
// state: arcade/challenge award tables, story sublevels, cheat menu ('f'),
// gallery ('k'), etc. Returns a sprite/texture id.
// ---------------------------------------------------------------------------
int loadscreen_select_background_texture(void)
{
    int result;

    if ((DAT_80501528 & 0x8000040) == 0) {
        if ((DAT_80501528 & 0x80000000) == 0) {
            if ((DAT_80501525 == '\n') && (DAT_80501524 == '8')) {
                result = 0x1a3;
            } else if (DAT_80501524 == 'f') {
                result = 0x2b9;
            } else {
                if (DAT_80501524 < 'f') {
                    if (DAT_80501524 == 'd') {
                        int *rec = FUN_80339b90(DAT_80612be8, 0xc77668);
                        if (rec != (int *)0) {
                            char *sub = FUN_8031d468((uint)*(unsigned short *)((int)rec + 10));
                            return *(int *)(sub + 0x50);
                        }
                        return 0x2b9;
                    }
                    if (DAT_80501524 > 'c') {
                        return 0x1a3;
                    }
                } else if (DAT_80501524 == 'k') {
                    return 0x18e;
                }
                if (DAT_80501525 == '\n') {
                    result = *(int *)(&DAT_803f2930 + DAT_805cc630 * 0x44);
                } else {
                    result = *(int *)(&DAT_803f31ec + DAT_805cc634 * 0x3c);
                }
            }
        } else {
            int awardId = FUN_80253df0(*(int *)(&DAT_80471c54 + DAT_805cc624 * 0x24));
            int slot = award_find_by_id(awardId);
            result = *(int *)(&DAT_803f2930 + slot * 0x44);
        }
    } else {
        char *rec = FUN_8022d9a4(DAT_805cc628);
        result = *(int *)(rec + 0x8c);
    }
    return result;
}

// ---------------------------------------------------------------------------
// loadscreen_init_tips().  Address: 0x8013381c
// Thin wrapper that (re)initialises the sequential tip-ID list.
// ---------------------------------------------------------------------------
void loadscreen_init_tips(void)
{
    loadtip_init_id_list();
}

// ---------------------------------------------------------------------------
// loadscreen_setup().  Address: 0x8013383c
// Full loading-screen setup performed before the load begins:
//   * derives the error/abort state flags from the tip/frame counter,
//   * primes the draw-list head pointers,
//   * (unless in the special 'g'/0x100000 mode) opens pak/loadbar.pak, resolves
//     the background texture path, streams it from DVD and registers it,
//   * resets the progress bars, chooses the layout mode from the sub-mode,
//   * seeds the tip scroll position and picks the first tip,
//   * arms phase 0 via loadscreen_set_phase(0).
// ---------------------------------------------------------------------------
void loadscreen_setup(unsigned long long r3, double f1, double f2, unsigned long long p4,
                      unsigned long long p5, unsigned long long p6, unsigned long long p7,
                      unsigned long long p8)
{
    FUN_803826f0();

    uint story = countLeadingZeros((uint)DAT_80612614) >> 5;
    DAT_80612054 = countLeadingZeros((uint)(0x67 - DAT_80612614)) >> 5;
    DAT_80612050 = story;

    DAT_805855c8 = (int)&DAT_805831a8;
    DAT_805855cc = (int)&DAT_805831a8;
    DAT_80611d54 = (int)&DAT_805853c4;

    if ((DAT_80501524 != 'g') && ((DAT_80501528 & 0x100000) == 0)) {
        int packSlot = pack_open(r3, f1, f2, p4, p5, p6, p7, p8,
                                 s_pak_loadbar_pak_8040d908, 0, 1, story, 0, 0, 0, 0);

        int texId = loadscreen_select_background_texture();
        void *texName = sprite_get_texture_name(texId);

        unsigned char pathBuf[128];
        FUN_80387fb8(r3, f1, f2, p4, p5, p6, p7, p8,
                     (int)(uintptr_t)pathBuf, s_assets_ts_textures_gamecube__s_g_8040d918,
                     texName, story, 0, 0, 0, 0);

        int handle = asset_load_from_dvd(r3, f1, f2, p4, p5, p6, p7, p8,
                                         pathBuf, 0, (uint *)0, story, 0, 0, 0, 0);

        if (asset_get_size(r3, f1, f2, p4, p5, p6, p7, p8, pathBuf) >= 0) {
            int size = asset_get_size(r3, f1, f2, p4, p5, p6, p7, p8, pathBuf);
            FUN_803994d0(handle, size);
        }

        if (handle == 0) {
            DAT_80612058 = sprite_get_texture_id(0x333);
        } else {
            FUN_8017bffc(r3, f1, f2, p4, p5, p6, p7, p8, handle, 0, &DAT_80612058, story, 0, 0, 0, 0);
        }

        if (packSlot != -1) {
            pack_destroy_slot();
        }
    }

    FLOAT_80612064 = FLOAT_80615ba8;
    loadtip_reset_bars();

    if (((DAT_80501528 & 0xa0000000) == 0) && (DAT_80501524 != 'e')) {
        if (DAT_80501525 == '\n') {
            FUN_801333cc(0);
        } else {
            FUN_801333cc(2);
        }
    } else {
        FUN_801333cc(1);
    }

    double widthF = (double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615bc8);
    FLOAT_806108e8 = FLOAT_80615bd8;
    DAT_806108e0 = 1;

    int scrollStart = (int)((double)FLOAT_80615c14 * widthF);
    FLOAT_80612060 = (float)((double)CONCAT44(0x43300000, (uint)scrollStart ^ 0x80000000) - DOUBLE_80615bc8);

    loadtip_pick_next(0);
    FUN_80133414(0);
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Loading-screen deferred render wrappers (batch_361)
//
// These three functions form the render-callback chain that drives the
// loading screen while a level streams in.  The game's render dispatcher
// (FUN_803a5f0c) takes a "next frame" callback; each of these wrappers
// pushes a render list, submits it, and re-arms the next callback so the
// loading animation keeps ticking across VI retraces without the main
// thread blocking.
//
//   loadscreen_render_with_progress  0x80133a34  draws bg + progress, ticks tips
//   loadscreen_render_deferred       0x80133aa0  submit + arm progress callback
//   loadscreen_wait_retrace_callback 0x80133ae8  VI sync + arm render callback
// ---------------------------------------------------------------------------

// Render-list plumbing (render subsystem, addresses in 0x8017xxxx).
extern void          FUN_8017df00(void);                       // begin render pass (clear/setup)
extern void          FUN_8017f4c8(void);                       // open render-list scope
extern void*         FUN_8017f3a4(void);                       // acquire current render-list handle
extern void          FUN_8017f538(void* rlHandle,              // submit render list + 7 register slots
                                  unsigned int a2, unsigned int a3, unsigned int a4,
                                  unsigned int a5, unsigned int a6, unsigned int a7,
                                  unsigned int a8);
extern void          FUN_8018259c(int enable, int mask);       // toggle render target/state
extern void          FUN_803a5f0c(void (*next)(void));         // arm next-frame render callback
extern void          VIWaitForRetrace(void);                   // Dolphin SDK: block until VI retrace

extern int           DAT_80612368;                             // "in loading render" reentrancy guard

// The render dispatcher (FUN_803a5f0c) invokes its callback with a fixed
// register set already loaded; the parameters the raw decomp shows as
// param_2..param_8 are those inbound registers, not an explicit call.  As a
// callback the function takes no meaningful C arguments, so it is modelled as
// void(void) and the register slots are forwarded to the submit helper as 0.
//
// 0x80133a34 : draw the loading background + progress bar, advancing the tip
// scroller when the background texture (sprite id 0x333) is resident.  Guards
// the submit with DAT_80612368 so nested render requests are suppressed.
void loadscreen_render_with_progress(void)
{
    FUN_8017df00();
    FUN_8018259c(-1, 0);
    FUN_8017f4c8();

    void* rlHandle = FUN_8017f3a4();

    int texId = sprite_get_texture_id(0x333);
    if (texId >= 0) {
        // When the background texture (sprite 0x333) is resident, tick the
        // loading animation; it may rebuild the render list, so re-acquire the
        // current handle afterwards.  (In the raw decomp loadscreen_tick's
        // return feeds the submit; here loadscreen_tick is void, so the handle
        // is re-read from FUN_8017f3a4, which returns the active render list.)
        loadscreen_tick();
        rlHandle = FUN_8017f3a4();
    }

    DAT_80612368 = 1;
    FUN_8017f538(rlHandle, 0, 0, 0, 0, 0, 0, 0);
    DAT_80612368 = 0;

    FUN_8018259c(1, -1);
}

// 0x80133aa0 : deferred variant - open a render list, submit the current
// payload, then arm loadscreen_render_with_progress as the next callback so
// the progress bar keeps updating on the following frame.
int loadscreen_render_deferred(void)
{
    FUN_8017f4c8();
    void* rlHandle = FUN_8017f3a4();
    FUN_8017f538(rlHandle, 0, 0, 0, 0, 0, 0, 0);
    FUN_8018259c(1, -1);
    FUN_803a5f0c(loadscreen_render_with_progress);
    return 0;
}

// 0x80133ae8 : wait for VI retrace, then arm FUN_8017df00 (begin-pass) as the
// next callback.  Used to pace the loading loop to the display refresh.
int loadscreen_wait_retrace_callback(void)
{
    VIWaitForRetrace();
    FUN_803a5f0c(FUN_8017df00);
    return 0;
}
