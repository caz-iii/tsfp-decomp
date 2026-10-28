// Address: 0x80119178 - 0x801193a0 | Batch 336 | Date: 2026-09-04
// HUD screen banner / letterbox fade system.
// A small state machine drives a full-width top+bottom letterbox banner used
// for cinematic/transition moments: show (fade in), show-persistent (snap to
// visible), hide (fade out), a per-frame tick that integrates the alpha, and a
// render pass that draws the two bars scaled by the current alpha.
// Subsystem: game (HUD rendering)
//
// State globals (raw addresses documented):
//   DAT_80611f7c    : banner mode/type (0 = letterbox bars)
//   FLOAT_80611f80  : current alpha/height factor [0..1]
//   DAT_80611f84    : phase: 1 = fading in, 0 = holding, -1 = fading out
//   DAT_80611f88    : hold timer (ms) / message handle
// Frame timing: FLOAT_806108ec = frame delta; DAT_806109c0 = period divisor.

#include "hud_internal.h"

// ---------------------------------------------------------------------------
// Shared externs (compile hardening).
// ---------------------------------------------------------------------------
extern int  DAT_80611748[];
extern char DAT_805015bd;         // current game mode byte
extern int  DAT_805015c0;         // mode flags
extern int  DAT_80612610;         // active player count
extern int  DAT_806108e4;         // frame delta (int ms)
extern int  DAT_806109c0;         // period divisor
extern float FLOAT_806108ec;      // frame delta (float)
extern float FLOAT_806157a8;      // rate constant
extern float FLOAT_806157a4;      // bar height scale
extern float FLOAT_80615718;      // 1.0f
extern float FLOAT_80615730;      // 0.0f
extern const double DOUBLE_80615738; // Ghidra int->float bias constant

extern int   DAT_80611f7c;
extern float FLOAT_80611f80;
extern int   DAT_80611f84;
extern int   DAT_80611f88;

extern int  FUN_8016e8ac(int blend);
extern void FUN_8016f00c(int mode);
extern void FUN_8016ee98(void);
extern void FUN_8016f4bc(int x1, int y1, int x2, int y2, int color);

// ============================================================================
// 0x80119178 | hud_banner_show
// Begins showing the banner (fade in). In 2+ player mapmaker mode a type-0
// banner is suppressed. Sets type, resets alpha to full, stores the message
// handle, and enters the fade-in phase (1).
// ============================================================================
void hud_banner_show(int message_handle, int type) {
    if (DAT_805015bd == '\n') {
        if ((1 < DAT_80612610) && ((DAT_805015c0 & 0x200) == 0) && (type == 0)) {
            return;
        }
    }
    DAT_80611f7c = type;
    FLOAT_80611f80 = FLOAT_80615718;
    DAT_80611f88 = message_handle;
    DAT_80611f84 = 0;
}

// ============================================================================
// 0x801191c8 | hud_banner_show_persistent
// Like hud_banner_show but enters the holding phase (1) directly (persistent
// display driven by a hold timer rather than an immediate fade-in).
// ============================================================================
void hud_banner_show_persistent(int message_handle, int type) {
    if (DAT_805015bd == '\n') {
        if ((1 < DAT_80612610) && ((DAT_805015c0 & 0x200) == 0) && (type == 0)) {
            return;
        }
    }
    DAT_80611f7c = type;
    DAT_80611f84 = 1;
    DAT_80611f88 = message_handle;
}

// ============================================================================
// 0x80119210 | hud_banner_hide
// Requests the banner to fade out (phase -1).
// ============================================================================
void hud_banner_hide(void) {
    DAT_80611f84 = -1;
}

// ============================================================================
// 0x8011921c | hud_banner_tick
// Per-frame banner state machine:
//   phase 1  (fading in): ramp alpha up toward 1.0; on reach, go to hold (0).
//   phase 0  (holding):   count the hold timer down by the frame delta; when it
//                         expires, switch to fade-out (-1).
//   phase -1 (fading out): ramp alpha down toward 0.0; on reach, go to hold (0).
// ============================================================================
void hud_banner_tick(void) {
    float step = FLOAT_806108ec *
                 (FLOAT_806157a8 /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738));

    if (DAT_80611f84 == 1) {
        if (FLOAT_80611f80 < FLOAT_80615718) {
            FLOAT_80611f80 = step + FLOAT_80611f80;
            if (FLOAT_80615718 < FLOAT_80611f80) {
                FLOAT_80611f80 = FLOAT_80615718;
                DAT_80611f84 = 0;
            }
        }
    } else if (DAT_80611f84 == 0) {
        if (-1 < DAT_80611f88) {
            if (0 < DAT_80611f88 - DAT_806108e4) {
                DAT_80611f88 = DAT_80611f88 - DAT_806108e4;
            } else {
                DAT_80611f84 = -1;
                DAT_80611f88 = 0;
            }
        }
    } else if (DAT_80611f84 == -1) {
        if (FLOAT_80615730 < FLOAT_80611f80) {
            FLOAT_80611f80 = FLOAT_80611f80 - step;
            if (FLOAT_80611f80 < FLOAT_80615730) {
                FLOAT_80611f80 = FLOAT_80615730;
                DAT_80611f84 = 0;
            }
        }
    }
}

// ============================================================================
// 0x80119328 | hud_banner_render
// Draws the top and bottom letterbox bars for a type-0 banner, each with a
// height of FLOAT_806157a4 * alpha, at half alpha (0x80). No-op unless a type-0
// banner is active with a positive alpha.
// ============================================================================
void hud_banner_render(void) {
    if (DAT_80611f7c == 0 && FLOAT_80615730 < FLOAT_80611f80) {
        int bar_h = (int)(FLOAT_806157a4 * FLOAT_80611f80);
        FUN_8016e8ac(0);
        FUN_8016f00c(1);
        FUN_8016ee98();
        FUN_8016f4bc(DAT_80611748[0], DAT_80611748[6] - bar_h,
                     DAT_80611748[1] + 1, DAT_80611748[6] + 1, 0x80);
        FUN_8016f4bc(DAT_80611748[0], DAT_80611748[5],
                     DAT_80611748[1] + 1, DAT_80611748[5] + bar_h + 1, 0x80);
    }
}
