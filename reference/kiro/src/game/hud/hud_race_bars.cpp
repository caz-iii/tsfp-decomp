// Batch 344 | Date: 2026-09-04
// HUD race/checkpoint progress bars. Drawn from hud_results_panel_update for the
// two-racer game modes (SRPG / SRP2 tags): each racer gets a labeled progress
// bar showing lap/checkpoint completion, with a small marker icon (sprite 0x119)
// at its endpoint. Widescreen adjusts the bar length.
//
//   0x8012810c hud_race_progress_bars
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful body; raw symbols declared
// per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615788;
extern float  FLOAT_806157ac;
extern float  FLOAT_806157c8;
extern float  FLOAT_80615838;
extern float  FLOAT_8061590c;
extern float  FLOAT_8061082c;
extern float  FLOAT_80610830;
extern double DOUBLE_80615738;  // uint->double bias

extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611d54;     // active render/camera context
extern int*   DAT_80611748;     // viewport rect (reassigned to per-racer rects)
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_8057ff4c, DAT_8057ff54, DAT_8057ff58; // layout insets

// named helpers (raw)
extern void   FUN_803826e0(void);   // HUD push (race bars)
extern void   FUN_8038272c(void);    // HUD pop  (race bars)
extern int    FUN_80229ee8(void);            // active race context (or 0)
extern int    FUN_8023c32c(int tag);         // find race-progress record by tag
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_8016e77c(int texId);
extern void   FUN_80170410(int x0, int y0, int x1, int y1, int u, int v, uint tw, uint th);
extern uint   is_widescreen(void);
extern int    get_string_by_id(int id);
extern void   hud_draw_stat_bar_labeled(double x0, double y0, double x1, double y1, double frac,
                                        double a, double b, double c, int d, int labelStr,
                                        int sprite, int flip);

// forward decl (this file)
extern "C" void hud_race_progress_bars(void);

// ============================================================================
// 0x8012810c | hud_race_progress_bars
// Draws the two racers' progress bars. Resolves the active race context and the
// two racer records (tags 'SRPG'/'SRP2'); for each, blits the marker icon
// (sprite 0x119) at the racer's viewport corner and draws a labeled stat bar
// (hud_draw_stat_bar_labeled) whose fill is checkpoint/total. Temporarily swaps
// DAT_80611748/DAT_80611d54 to each racer's viewport rect.
// ============================================================================
extern "C" void hud_race_progress_bars(void) {
    FUN_803826e0();
    int ctx = FUN_80229ee8();
    int racer1 = FUN_8023c32c(0x53505247);   // 'SRPG'
    int racer2 = FUN_8023c32c(0x53505232);   // 'SRP2'
    if (ctx != 0) {
        // --- racer 1: local player viewport ---
        DAT_80611748 = (int*)(*(int*)(DAT_80611d74 + 8) + 0x224);
        int icon = sprite_get_texture_id(0x119);
        int rx = *DAT_80611748;
        int rw = DAT_80611748[4];
        int ry = DAT_80611748[5];
        int rh = DAT_80611748[9];
        FUN_8016e77c(icon);
        uint cxMag = rx + rw ^ 0x80000000;
        uint cyMag = ry + rh ^ 0x80000000;
        int desc = *DAT_80610a20 + icon * 0x58;
        double cx = (double)((double)CONCAT44(0x43300000, cxMag) - DOUBLE_80615738);
        double cy = (double)((double)CONCAT44(0x43300000, cyMag) - DOUBLE_80615738);
        FUN_80170410((int)((float)cx - FLOAT_80615788 * FLOAT_8061082c),
                     (int)((float)cy - FLOAT_80615788 * FLOAT_80610830),
                     (int)((float)cx + FLOAT_80615788 * FLOAT_8061082c),
                     (int)((float)cy + FLOAT_80615788 * FLOAT_80610830), 0, 0,
                     (uint)*(unsigned short*)(desc + 0x4e) << 4, (uint)*(unsigned short*)(desc + 0x52) << 4);

        double frac1 = (double)(*(float*)(racer1 + 0x1cc) / *(float*)(racer1 + 0x1d8));
        double f1 = frac1; if ((double)FLOAT_80615718 < frac1) f1 = (double)FLOAT_80615718;
        double clamp1 = (double)FLOAT_80615730;
        if ((clamp1 <= f1) && (clamp1 = frac1, (double)FLOAT_80615718 < frac1)) clamp1 = (double)FLOAT_80615718;
        float wide = FLOAT_80615718; if ((is_widescreen() & 0xff) != 0) wide = FLOAT_8061590c;
        uint by = DAT_80611748[6] - DAT_8057ff58 ^ 0x80000000;
        uint lx = DAT_8057ff4c + *DAT_80611748 ^ 0x80000000;
        int str1 = get_string_by_id(0x670);
        hud_draw_stat_bar_labeled(
            (double)(float)((double)CONCAT44(0x43300000, lx) - DOUBLE_80615738),
            -(double)(FLOAT_806157ac * FLOAT_80610830 - (float)((double)CONCAT44(0x43300000, by) - DOUBLE_80615738)),
            (double)(float)((double)(FLOAT_806157c8 * FLOAT_8061082c) * (double)wide +
                            (double)(float)((double)CONCAT44(0x43300000, lx) - DOUBLE_80615738)),
            (double)(float)((double)CONCAT44(0x43300000, by) - DOUBLE_80615738), clamp1,
            (double)FLOAT_80615838, (double)FLOAT_80615838, (double)FLOAT_80615718, 0, str1, 0xdf, 0);

        // --- racer 2: swap to its viewport/context ---
        int savedCtx = DAT_80611d54;
        int* savedRect = DAT_80611748;
        DAT_80611748 = (int*)(ctx + 0x224);
        int r2x = *(int*)(ctx + 0x224);
        int r2w = *(int*)(ctx + 0x234);
        int r2y = *(int*)(ctx + 0x238);
        int r2h = *(int*)(ctx + 0x248);
        DAT_80611d54 = ctx;
        FUN_8016e77c(icon);
        uint cx2Mag = r2x + r2w ^ 0x80000000;
        uint cy2Mag = r2y + r2h ^ 0x80000000;
        int desc2 = *DAT_80610a20 + icon * 0x58;
        double cx2 = (double)((double)CONCAT44(0x43300000, cx2Mag) - DOUBLE_80615738);
        double cy2 = (double)((double)CONCAT44(0x43300000, cy2Mag) - DOUBLE_80615738);
        FUN_80170410((int)((float)cx2 - FLOAT_80615788 * FLOAT_8061082c),
                     (int)((float)cy2 - FLOAT_80615788 * FLOAT_80610830),
                     (int)((float)cx2 + FLOAT_80615788 * FLOAT_8061082c),
                     (int)((float)cy2 + FLOAT_80615788 * FLOAT_80610830), 0, 0,
                     (uint)*(unsigned short*)(desc2 + 0x4e) << 4, (uint)*(unsigned short*)(desc2 + 0x52) << 4);

        double frac2 = (double)(*(float*)(racer2 + 0x1cc) / *(float*)(racer2 + 0x1d8));
        double f2 = frac2; if ((double)FLOAT_80615718 < frac2) f2 = (double)FLOAT_80615718;
        double clamp2 = (double)FLOAT_80615730;
        if ((clamp2 <= f2) && (clamp2 = frac2, (double)FLOAT_80615718 < frac2)) clamp2 = (double)FLOAT_80615718;
        float wide2 = FLOAT_80615718; if ((is_widescreen() & 0xff) != 0) wide2 = FLOAT_8061590c;
        uint by2 = *(int*)((int)DAT_80611748 + 0x18) - DAT_8057ff58 ^ 0x80000000;
        uint rx2 = *(int*)((int)DAT_80611748 + 4) - DAT_8057ff54 ^ 0x80000000;
        int str2 = get_string_by_id(0x671);
        hud_draw_stat_bar_labeled(
            -(double)(float)((double)(FLOAT_806157c8 * FLOAT_8061082c) * (double)wide2 -
                             (double)(float)((double)CONCAT44(0x43300000, rx2) - DOUBLE_80615738)),
            -(double)(FLOAT_806157ac * FLOAT_80610830 - (float)((double)CONCAT44(0x43300000, by2) - DOUBLE_80615738)),
            (double)(float)((double)CONCAT44(0x43300000, rx2) - DOUBLE_80615738),
            (double)(float)((double)CONCAT44(0x43300000, by2) - DOUBLE_80615738), clamp2,
            (double)FLOAT_80615838, (double)FLOAT_80615838, (double)FLOAT_80615718, 0, str2, 0xdf, 1);

        DAT_80611748 = savedRect;
        DAT_80611d54 = savedCtx;
    }
    FUN_8038272c();
    return;
}
