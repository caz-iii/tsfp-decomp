// Batch 346 | Date: 2026-09-04
// HUD tutorial / objective hint screens. These build the scripted button-prompt
// hint layouts (one per game-mode context) using the hint command-record queue
// (hud_hint_* API from batch 345), then hand off to the hint renderer
// (FUN_8012ced8). Each hint screen is "shown once" per player, gated by a bit in
// the per-player flags word (DAT_80582d94). Per-player hint state lives in the
// DAT_80582944 block (0x114 stride).
//
// Shared builder API:
//   0x8012b3a4 hud_hint_begin              (set anchor/position + reset queue)
//   0x8012b40c hud_hint_add_command        (add a 3-word command to the current record)
//   0x8012b438 hud_hint_add_arg            (append one arg to the current record)
//   0x8012b464 hud_hint_add_line           (add command + arg0, advance to next record)
//   0x8012b4d0 hud_hint_system_reset_all   (reset all 4 players' hint state)
//   0x8012b524 hud_hint_reset_shown_flags  (clear per-player shown bits)
//
// Mode-specific hint screens (each shown-once via a flag bit):
//   0x8012b578 hud_hint_show_grab_controls   (bit 0x01, objective grab/throw help)
//   0x8012b748 hud_hint_show_throw_control   (bit 0x04)
//   0x8012b8dc hud_hint_show_generic         (bit 0x8000, string 0x178e)
//   0x8012ba50 hud_hint_show_swap_controls   (bit 0x1000, strings 0x1788/0x1789)
//
// Subsystem: game (HUD)
//
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_8061575c;
extern float  FLOAT_80615874;
extern float  FLOAT_8061082c;
extern float  FLOAT_80610830;
extern double DOUBLE_80615738;  // uint->double bias

extern int    DAT_80611748[];   // viewport rect
extern int    DAT_80611d78;     // player-slot table base (0x141c stride)
extern int    DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54; // layout insets
extern int    DAT_80612610;     // active player count
extern int    DAT_80612614;     // special objective id
extern int    DAT_806109c8;     // render height
extern unsigned char DAT_805015bd;   // game mode byte
extern int    DAT_80582944;     // per-player hint state block (0x114 stride, via &NAME)
extern int    DAT_80582a24;     // per-player hint line count (0x114 stride, via &NAME)
extern int    DAT_80582d94;     // per-player hint "shown" flags word (via &NAME)

// named helpers (raw) — hint queue API (batch 345)
extern void   hud_hint_record_set3(int* rec, int a, int b, int c);
extern void   hud_hint_record_append_arg(int rec, int arg);
extern void   hud_hint_record_set_arg0(int rec, int arg);
extern void   hud_hint_system_reset(void);
extern void   hud_hint_system_clear(int state);
extern int    FUN_803826f0(void);   // HUD push (hint builder), returns active view index
extern void   FUN_8038273c(void);    // HUD pop  (hint builder)
extern unsigned long long FUN_803826ec(void);  // HUD push variant (view index in high word)
extern void   FUN_80382738(void);               // HUD pop  variant
// hud_hint_panel_measure (0x8012ced8) is defined at the bottom of this file.
unsigned long long hud_hint_panel_measure(unsigned long long rec, double, double, double, double,
                                          double, unsigned long long, unsigned long long);

// forward decls (this file)
extern "C" {
void hud_hint_begin(int state, int x, int y, int width, int slot);
void hud_hint_add_command(int state, int cmd, int a, int b);
void hud_hint_add_arg(int state, int arg);
void hud_hint_add_line(int state, int cmd, int arg0, int arg1);
void hud_hint_system_reset_all(void);
void hud_hint_reset_shown_flags(void);
void hud_hint_show_grab_controls(void);
void hud_hint_show_throw_control(void);
void hud_hint_show_generic(void);
void hud_hint_show_swap_controls(void);
}

// ============================================================================
// 0x8012b3a4 | hud_hint_begin
// Begin a hint block for a state block: store the anchor (x,y), width, mark it
// active (+0xf4=1), copy the player's cursor/color context (+0x108..0x110 from
// the slot block), and reset the command queue.
// ============================================================================
extern "C" void hud_hint_begin(int state, int x, int y, int width, int slot) {
    int off = slot * 0x141c;
    *(int*)(state + 0xe4) = x;
    *(int*)(state + 0xe8) = y;
    *(int*)(state + 0xec) = width;
    *(int*)(state + 0xf4) = 1;
    *(int*)(state + 0x108) = *(int*)(DAT_80611d78 + off + 0xc0);
    *(int*)(state + 0x10c) = *(int*)(DAT_80611d78 + off + 0xc4);
    *(int*)(state + 0x110) = *(int*)(DAT_80611d78 + off + 0xc8);
    hud_hint_system_reset();
    return;
}

// ============================================================================
// 0x8012b40c | hud_hint_add_command
// Store a 3-word command into the current record (index at +0xe0, 0x1c stride).
// ============================================================================
extern "C" void hud_hint_add_command(int state, int cmd, int a, int b) {
    hud_hint_record_set3((int*)(state + *(int*)(state + 0xe0) * 0x1c), cmd, a, b);
    return;
}

// ============================================================================
// 0x8012b438 | hud_hint_add_arg
// Append one arg to the current record's arg list.
// ============================================================================
extern "C" void hud_hint_add_arg(int state, int arg) {
    hud_hint_record_append_arg(state + *(int*)(state + 0xe0) * 0x1c, arg);
    return;
}

// ============================================================================
// 0x8012b464 | hud_hint_add_line
// Add a command line (command word + first arg) to the current record, then
// advance the record index (+0xe0).
// ============================================================================
extern "C" void hud_hint_add_line(int state, int cmd, int arg0, int arg1) {
    hud_hint_record_set3((int*)(state + *(int*)(state + 0xe0) * 0x1c), cmd, arg1, 0);
    hud_hint_record_set_arg0(state + *(int*)(state + 0xe0) * 0x1c, arg0);
    *(int*)(state + 0xe0) = *(int*)(state + 0xe0) + 1;
    return;
}

// ============================================================================
// 0x8012b4d0 | hud_hint_system_reset_all
// Reset all 4 players' hint state blocks (DAT_80582944, 0x114 stride).
// ============================================================================
extern "C" void hud_hint_system_reset_all(void) {
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < 4; i++) {
        hud_hint_system_clear((int)&DAT_80582944 + off);
        off += 0x114;
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012b524 | hud_hint_reset_shown_flags
// Clear the per-player "hint shown" flags. In 2+ player splitscreen story mode
// the low 16 bits are pre-set (those hints are suppressed for splitscreen).
// ============================================================================
extern "C" void hud_hint_reset_shown_flags(void) {
    int players = DAT_80612610;
    char mode = DAT_805015bd;
    int off = 0;
    for (int i = 4; i != 0; i--) {
        *(int*)((int)&DAT_80582d94 + off) = 0;
        if ((mode == '\n') && (1 < players)) {
            *(uint*)((int)&DAT_80582d94 + off) = *(uint*)((int)&DAT_80582d94 + off) | 0xffff;
        }
        off += 4;
    }
    return;
}

// Helper: shared layout math for the mode-specific hint screens. Computes the
// anchor (x,y) and width used by hud_hint_begin, plus the render args passed to
// FUN_8012ced8. Splitscreen player 1 is offset to the lower half.
static void hud_hint_layout(int view, int* outX, int* outY, double* outSc, double* outW) {
    int yShift = 0;
    if (view != 0) {
        yShift = ((int)DAT_806109c8 >> 1) + (uint)((int)DAT_806109c8 < 0 && (DAT_806109c8 & 1) != 0);
    }
    double sy = (double)FLOAT_8061082c;
    double sc = (double)FLOAT_8061575c;
    double w = (double)(FLOAT_80615874 *
        (float)((double)CONCAT44(0x43300000,
                 (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000) - DOUBLE_80615738));
    int x = (int)(sy * w +
        (double)(float)((double)CONCAT44(0x43300000, DAT_8057ff4c + *DAT_80611748 ^ 0x80000000) - DOUBLE_80615738));
    int y = (int)(sc * (double)FLOAT_80610830 +
        (double)(float)((double)CONCAT44(0x43300000, DAT_8057ff50 + DAT_80611748[5] + yShift ^ 0x80000000) - DOUBLE_80615738));
    *outX = x; *outY = y; *outSc = sc; *outW = w;
}

// ============================================================================
// 0x8012b578 | hud_hint_show_grab_controls
// Objective-mode (DAT_80612614==2) grab/throw controls hint. Shown once per
// player (flag bit 0x01). Builds a title line (0x16d5) + four button+label lines
// (0x1772/0x177a/0x177e/0x1783), then renders it.
// ============================================================================
extern "C" void hud_hint_show_grab_controls(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (DAT_80612614 == 2) {
            if (((&DAT_80582d94)[view] & 1) == 0) {
                int off = view * 0x114;
                (&DAT_80582d94)[view] |= 1;
                int state = (int)&DAT_80582944 + off;
                hud_hint_system_reset();
                int x, y; double sc, w;
                hud_hint_layout(view, &x, &y, &sc, &w);
                hud_hint_begin(state, x, y, 200, view);
                hud_hint_add_command(state, 0x16d5, 3, 1);
                *(int*)((int)&DAT_80582a24 + off) += 1;
                hud_hint_add_line(state, 0x1772, 0, 0x12);
                hud_hint_add_line(state, 0x177a, 0xd, 0x12);
                hud_hint_add_line(state, 0x177e, 3, 0x12);
                unsigned long long rec = hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
                // (original passes the last add_line's record; behaviour preserved)
                hud_hint_add_line(state, 0x1783, 0xb, 0x12);
                hud_hint_panel_measure(rec, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
            }
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012b748 | hud_hint_show_throw_control
// Objective-mode throw-control hint (flag bit 0x04). Title line (0x1702) + one
// button+label line (0x177f), then render.
// ============================================================================
extern "C" void hud_hint_show_throw_control(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (DAT_80612614 == 2) {
            if (((&DAT_80582d94)[view] & 4) == 0) {
                int off = view * 0x114;
                (&DAT_80582d94)[view] |= 4;
                int state = (int)&DAT_80582944 + off;
                hud_hint_system_reset();
                int x, y; double sc, w;
                hud_hint_layout(view, &x, &y, &sc, &w);
                hud_hint_begin(state, x, y, 200, view);
                hud_hint_add_command(state, 0x1702, 3, 1);
                *(int*)((int)&DAT_80582a24 + off) += 1;
                hud_hint_add_line(state, 0x177f, 1, 0x12);
                hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
            }
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012b8dc | hud_hint_show_generic
// Generic single-title hint (flag bit 0x8000, string 0x178e), any mode. Shown
// once per player.
// ============================================================================
extern "C" void hud_hint_show_generic(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (((&DAT_80582d94)[view] & 0x8000) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x8000;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin((int)&DAT_80582944 + off, x, y, 200, view);
            hud_hint_add_command((int)&DAT_80582944 + off, 0x178e, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012ba50 | hud_hint_show_swap_controls
// Weapon/item swap controls hint (flag bit 0x1000). Title line (0x16ea) + two
// command lines (0x1788/0x1789) each with two appended args, then render.
// NB: unlike the others this does not set the shown flag (bit test only).
// ============================================================================
extern "C" void hud_hint_show_swap_controls(void) {
    int view = FUN_803826f0();
    if ((-1 < view) && (((&DAT_80582d94)[view] & 0x1000) == 0)) {
        int off = view * 0x114;
        int state = (int)&DAT_80582944 + off;
        hud_hint_system_reset();
        int x, y; double sc, w;
        hud_hint_layout(view, &x, &y, &sc, &w);
        hud_hint_begin(state, x, y, 200, view);
        hud_hint_add_command(state, 0x16ea, 3, 1);
        *(int*)((int)&DAT_80582a24 + off) += 1;
        hud_hint_add_command(state, 0x1788, 0x12, 0);
        *(int*)((int)&DAT_80582a24 + off) += 1;
        hud_hint_add_command(state, 0x1789, 0x12, 0);
        hud_hint_add_arg(state, 0);
        hud_hint_add_arg(state, 0);
        *(int*)((int)&DAT_80582a24 + off) += 1;
        hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// Batch 346 (appended): additional mode-specific hint screens. Same shown-once
// pattern as above (flag bit in DAT_80582d94, layout via hud_hint_layout, build
// via the hint queue, render via FUN_8012ced8). The vehicle-related ones use the
// FUN_803826ec/FUN_80382738 push/pop variant (view index in the high word) and a
// dynamic title command id returned by that push.
// ============================================================================

extern "C" {
void hud_hint_show_empty(void);
void hud_hint_show_mode4_controls(void);
void hud_hint_show_pickup_controls(void);
void hud_hint_show_defuse_controls(void);
void hud_hint_show_combo_controls(void);
void hud_hint_show_vehicle_controls(void);
void hud_hint_show_vehicle_exit(void);
void hud_hint_show_vehicle_fire(void);
void hud_hint_show_vehicle_brief(void);
void hud_hint_show_reload(void);
}

// ============================================================================
// 0x8012bc14 | hud_hint_show_empty
// Shown-once (bit 0x2000) placeholder hint: begins a block and renders it with
// no command lines (bumps the line count once). Used to reserve/clear the panel.
// ============================================================================
extern "C" void hud_hint_show_empty(void) {
    int view = FUN_803826f0();
    if ((-1 < view) && (((&DAT_80582d94)[view] & 0x2000) == 0)) {
        int off = view * 0x114;
        hud_hint_system_reset();
        int x, y; double sc, w;
        hud_hint_layout(view, &x, &y, &sc, &w);
        hud_hint_begin((int)&DAT_80582944 + off, x, y, 200, view);
        *(int*)((int)&DAT_80582a24 + off) += 1;
        hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012bd6c | hud_hint_show_mode4_controls
// Mode-4 (DAT_80612614==4) controls hint (bit 0x08). Title 0x170f + command
// 0x178d + button+label line 0x178c.
// ============================================================================
extern "C" void hud_hint_show_mode4_controls(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (DAT_80612614 == 4) {
            if (((&DAT_80582d94)[view] & 8) == 0) {
                int off = view * 0x114;
                (&DAT_80582d94)[view] |= 8;
                int state = (int)&DAT_80582944 + off;
                hud_hint_system_reset();
                int x, y; double sc, w;
                hud_hint_layout(view, &x, &y, &sc, &w);
                hud_hint_begin(state, x, y, 200, view);
                hud_hint_add_command(state, 0x170f, 3, 1);
                *(int*)((int)&DAT_80582a24 + off) += 1;
                hud_hint_add_command(state, 0x178d, 0x12, 1);
                *(int*)((int)&DAT_80582a24 + off) += 1;
                hud_hint_add_line(state, 0x178c, 0x11, 0x12);
                hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
            }
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012bf20 | hud_hint_show_pickup_controls
// Pickup/use controls hint (bit 0x10). Title 0x15e2 + two button+label lines
// (0x1701 / 0x16ec).
// ============================================================================
extern "C" void hud_hint_show_pickup_controls(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (((&DAT_80582d94)[view] & 0x10) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x10;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, 0x15e2, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x1701, 0x1a, 0x12);
            hud_hint_add_line(state, 0x16ec, 0x18, 0x12);
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012c0bc | hud_hint_show_defuse_controls
// Objective-mode (DAT_80612614==2) defuse/plant controls hint (bit 0x20). Title
// 0x16d0 + two button+label lines (0x1784 / 0x1785).
// ============================================================================
extern "C" void hud_hint_show_defuse_controls(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (DAT_80612614 == 2) {
            if (((&DAT_80582d94)[view] & 0x20) == 0) {
                int off = view * 0x114;
                (&DAT_80582d94)[view] |= 0x20;
                int state = (int)&DAT_80582944 + off;
                hud_hint_system_reset();
                int x, y; double sc, w;
                hud_hint_layout(view, &x, &y, &sc, &w);
                hud_hint_begin(state, x, y, 200, view);
                hud_hint_add_command(state, 0x16d0, 3, 1);
                *(int*)((int)&DAT_80582a24 + off) += 1;
                hud_hint_add_line(state, 0x1784, 4, 0x12);
                hud_hint_add_line(state, 0x1785, 0x1b, 0x12);
                hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
            }
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012c264 | hud_hint_show_combo_controls
// Combined controls hint (bit 0x40). Title 0x16c6 + three button+label lines
// (0x1772 / 0x1784 / 0x1785).
// ============================================================================
extern "C" void hud_hint_show_combo_controls(void) {
    int view = FUN_803826f0();
    if (-1 < view) {
        if (((&DAT_80582d94)[view] & 0x40) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x40;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, 0x16c6, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x1772, 0, 0x12);
            hud_hint_add_line(state, 0x1784, 4, 0x12);
            hud_hint_add_line(state, 0x1785, 0x1b, 0x12);
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012c414 | hud_hint_show_vehicle_controls
// Vehicle controls hint (bit 0x100). Uses the FUN_803826ec push variant, whose
// return supplies both the view index (high word) and the dynamic title command
// id (low word). Title + three button+label lines (0x15b7/0x15b8/0x15b5) + two
// commands (0x15cc/0x15cb).
// ============================================================================
extern "C" void hud_hint_show_vehicle_controls(void) {
    unsigned long long push = FUN_803826ec();
    int view = (int)(push >> 0x20);
    int title = (int)push;
    if (-1 < (long long)push) {
        if (((&DAT_80582d94)[view] & 0x100) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x100;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, title, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x15b7, 0x1a, 0x12);
            hud_hint_add_line(state, 0x15b8, 0x18, 0x12);
            hud_hint_add_line(state, 0x15b5, 0x15, 0x12);
            hud_hint_add_command(state, 0x15cc, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_command(state, 0x15cb, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8012c608 | hud_hint_show_vehicle_exit
// Vehicle exit hint (bit 0x400). Dynamic title + line 0x15ba + line 0x15b5, then
// a mount-state-dependent command (0x15cf if slot +0x6c==1, else 0x15d0).
// ============================================================================
extern "C" void hud_hint_show_vehicle_exit(void) {
    unsigned long long push = FUN_803826ec();
    int view = (int)(push >> 0x20);
    int title = (int)push;
    if (-1 < (long long)push) {
        if (((&DAT_80582d94)[view] & 0x400) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x400;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, title, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x15ba, 0x1a, 0x12);
            hud_hint_add_line(state, 0x15b5, 0x15, 0x12);
            if (*(int*)(DAT_80611d78 + view * 0x141c + 0x6c) == 1) {
                hud_hint_add_command(state, 0x15cf, 3, 1);
            } else {
                hud_hint_add_command(state, 0x15d0, 3, 1);
            }
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8012c7f8 | hud_hint_show_vehicle_fire
// Vehicle fire hint (bit 0x200). Dynamic title + line 0x15ba + commands
// 0x15cd / 0x15d0.
// ============================================================================
extern "C" void hud_hint_show_vehicle_fire(void) {
    unsigned long long push = FUN_803826ec();
    int view = (int)(push >> 0x20);
    int title = (int)push;
    if (-1 < (long long)push) {
        if (((&DAT_80582d94)[view] & 0x200) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x200;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, title, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x15ba, 0x1a, 0x12);
            hud_hint_add_command(state, 0x15cd, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_command(state, 0x15d0, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8012c9c4 | hud_hint_show_vehicle_brief
// Brief vehicle hint (bit 0x800). Dynamic title + line 0x15ba + a mount-state
// command (0x15cf if slot +0x6c==1, else 0x15d0).
// ============================================================================
extern "C" void hud_hint_show_vehicle_brief(void) {
    unsigned long long push = FUN_803826ec();
    int view = (int)(push >> 0x20);
    int title = (int)push;
    if (-1 < (long long)push) {
        if (((&DAT_80582d94)[view] & 0x800) == 0) {
            int off = view * 0x114;
            (&DAT_80582d94)[view] |= 0x800;
            int state = (int)&DAT_80582944 + off;
            hud_hint_system_reset();
            int x, y; double sc, w;
            hud_hint_layout(view, &x, &y, &sc, &w);
            hud_hint_begin(state, x, y, 200, view);
            hud_hint_add_command(state, title, 3, 1);
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_add_line(state, 0x15ba, 0x1a, 0x12);
            if (*(int*)(DAT_80611d78 + view * 0x141c + 0x6c) == 1) {
                hud_hint_add_command(state, 0x15cf, 3, 1);
            } else {
                hud_hint_add_command(state, 0x15d0, 3, 1);
            }
            *(int*)((int)&DAT_80582a24 + off) += 1;
            hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8012cba0 | hud_hint_show_reload
// Reload hint (bit 0x80). Single command line (0x1782), then render.
// ============================================================================
extern "C" void hud_hint_show_reload(void) {
    int view = FUN_803826f0();
    if ((-1 < view) && (((&DAT_80582d94)[view] & 0x80) == 0)) {
        int off = view * 0x114;
        hud_hint_system_reset();
        int x, y; double sc, w;
        hud_hint_layout(view, &x, &y, &sc, &w);
        hud_hint_begin((int)&DAT_80582944 + off, x, y, 200, view);
        hud_hint_add_command((int)&DAT_80582944 + off, 0x1782, 0x12, 0);
        *(int*)((int)&DAT_80582a24 + off) += 1;
        hud_hint_panel_measure(0, sc, (double)(long long)x, w, DOUBLE_80615738, (double)FLOAT_8061082c, 0, 0);
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// Batch 348 | Date: 2026-09-04
// Hint-panel widget core: the measure pass, the main render + fade state
// machine, the "begin close" state setter, and one more shown-once screen
// (objective grab). These are the functions the batch 345-347 hint builders
// hand off to. Addresses: hud_hint_panel_measure 0x8012ced8,
// hud_hint_panel_render 0x8012d1a8, hud_hint_panel_begin_close 0x8012cea8,
// hud_hint_show_objective_grab 0x8012cd0c.
// ============================================================================

// --- additional per-file externs for the widget core ---
extern float  FLOAT_8060f958;
extern float  FLOAT_806157ac;
extern float  FLOAT_806157c8;
extern float  FLOAT_806157d4;
extern float  FLOAT_806159f8;
extern float  FLOAT_80615718;
extern float  FLOAT_80615730;
extern float  FLOAT_80615748;
extern float  FLOAT_80615760;
extern float  FLOAT_80615768;
extern float  FLOAT_80615780;
extern float  FLOAT_80615788;
extern float  FLOAT_806157f4;
extern float  FLOAT_80615868;
extern float  FLOAT_80615884;
extern float  FLOAT_806157e8;
extern float  FLOAT_8061571c;
extern float  FLOAT_80615780;
extern float  FLOAT_80610830;
extern float  FLOAT_80615a10;
extern float  FLOAT_80615a14;
extern float  FLOAT_806108e8;
extern double DOUBLE_80615a00;
extern double DOUBLE_80615a08;
extern int    DAT_806109c0;
extern int    DAT_805015c0;
extern int    DAT_80611d74;      // active HUD context ptr
extern int    DAT_8057ff4c;
extern float  FLOAT_80615780;

// per-player hint panel record fields (0x114 stride, addressed via &NAME + off):
extern int    DAT_80582a28;      // +0xe4 running X cursor
extern int    DAT_80582a2c;      // +0xe8 base Y
extern int    DAT_80582a30;      // +0xec measured width
extern int    DAT_80582a34;      // +0xf0 measured height
extern int    DAT_80582a38;      // +0xf4 active flag
extern int    DAT_80582a3c;      // +0xf8 fade state (0..3)
extern int    DAT_80582a40;      // +0xfc fade alpha
extern int    DAT_80582a44;      // +0x100 fade timer
extern int    DAT_80582a48;      // +0x104 distance accumulator
extern int    DAT_80582a4c;      // +0x108 anchor x
extern int    DAT_80582a50;      // +0x10c anchor y
extern int    DAT_80582a54;      // +0x110 anchor z

// helpers (raw / named)
extern int    game_is_mapmaker_multiplayer(void);
extern void   hud_charge_meter_render(double x, double y);
extern int    hud_hint_get_command_field(int packed, int view);
extern int    hud_is_special_layout(void);
extern void   hud_compute_fade_channel(int ch);
extern int    sprite_get_texture_id(int id);
extern double screen_effect_get_damage_alpha(void);
extern int    get_string_by_id(int id);
extern void   text_set_font_table(int table);
extern void   text_draw_char(double x, double y, void* text, unsigned int color);
extern void   text_draw_char_shadowed(double x, double y, void* text, unsigned int color,
                                       unsigned int shadow);
extern double text_get_char_height(void* text);
extern double text_get_char_width(void* text);
extern void*  text_check_scale(double w, void* text);
extern void   text_set_color_rgb(double r, double g, double b);
extern double text_set_alpha(double a);
extern void   input_get_button_name(int btn, int* out);
// hud_format_button_prompt (0x801044a0) and format_string_varargs (0x80006218):
// cross-module formatters. This TU includes only common.h (not generated_decls),
// so declare them per-file with the canonical 16-slot ABI.
extern unsigned long long hud_format_button_prompt(
        unsigned long long acc, double d1, double d2, double d3, double d4, double d5,
        unsigned long long g6, unsigned long long g7,
        int button_id, int flag, int dest_buf, void* a, void* b, void* c,
        unsigned int d, unsigned int e);
extern void format_string_varargs(
        unsigned long long acc, double d1, double d2, double d3, double d4, double d5,
        unsigned long long g6, unsigned long long g7,
        int* out_buf, int fmt, void* a, void* b, void* c, void* d,
        unsigned int e, unsigned int f);

extern unsigned long long FUN_803826d8(void);  // HUD push (render), view index
extern void   FUN_80382724(void);               // HUD pop  (render)
extern unsigned long long FUN_803826e4(void);   // HUD push (measure), view ptr in high word
extern void   FUN_80382730(void);               // HUD pop  (measure)
extern int    DAT_806108c4;      // format string: "%s label"
extern int    DAT_806108c8;      // format string: "%s %s label"
extern int    DAT_806108cc;      // format string: "%s %s %s label"
extern int    FUN_803825c0(double v);            // damage-flash color byte
extern int    FUN_802448f8(void);                // active gameplay object ptr
extern void   FUN_8038b510(int* dst, int* src);  // string copy into buffer
extern void   FUN_8016f24c(int mode, int x0, int y0, int x1, int y1, int z); // panel bg
extern void   FUN_802e42c0(int x0, int y0, int x1, unsigned int y1, int flag); // edge strip

// forward decl for the extra shown-once screen so the render TU below is self-contained
extern "C" void hud_hint_show_objective_grab(unsigned long long, double, double, unsigned long long,
                                             unsigned long long, unsigned long long,
                                             unsigned long long, unsigned long long);

// ----------------------------------------------------------------------------
// hud_hint_panel_measure (0x8012ced8)
// Measures the pixel width of the hint panel by formatting each line the same
// way the renderer does and taking the widest. Stores result at record +0xec.
// ----------------------------------------------------------------------------
unsigned long long hud_hint_panel_measure(unsigned long long param_1, double param_2, double param_3,
                                          double param_4, double param_5, double param_6,
                                          unsigned long long param_7, unsigned long long param_8)
{
    int iVar1;
    int* piVar3;
    unsigned char* pbVar4;
    int iVar5;
    unsigned char* in_r6 = 0;
    unsigned char* in_r7 = 0;
    unsigned char* in_r8 = 0;
    unsigned int in_r9 = 0;
    unsigned int in_r10 = 0;
    unsigned int uVar6;
    int iVar7;
    int* piVar8;
    unsigned int uVar9;
    int iVar10;
    double dVar11 = 0.0;
    unsigned long long uVar12;
    unsigned char auStack_278[64];
    unsigned char auStack_238[64];
    unsigned char abStack_1f8[64];
    unsigned char abStack_1b8[384];
    long long local_38;

    uVar12 = FUN_803826e4();
    int iVar2 = (int)(uVar12 >> 0x20);
    int iVar5b = (int)uVar12;
    if (-1 <= iVar5b) {
        iVar5 = iVar5b;
        uVar6 = 0;
        iVar10 = 0;
        for (iVar7 = 0; iVar7 < *(int*)(iVar2 + 0xe0); iVar7 = iVar7 + 1) {
            piVar8 = (int*)(iVar2 + iVar10);
            iVar1 = piVar8[6];
            if (iVar1 == 2) {
                iVar1 = hud_hint_get_command_field(piVar8[3], iVar5);
                uVar12 = hud_format_button_prompt(dVar11, param_2, param_3, param_4, param_5, param_6,
                        param_7, param_8, iVar1, 0, (int)abStack_1f8, in_r6, in_r7, in_r8, in_r9, in_r10);
                iVar1 = hud_hint_get_command_field(piVar8[4], iVar5);
                uVar12 = hud_format_button_prompt(uVar12, param_2, param_3, param_4, param_5, param_6,
                        param_7, param_8, iVar1, 0, (int)auStack_238, in_r6, in_r7, in_r8, in_r9, in_r10);
                iVar1 = get_string_by_id(*piVar8);
                in_r6 = abStack_1f8;
                in_r7 = auStack_238;
                format_string_varargs(uVar12, param_2, param_3, param_4, param_5, param_6, param_7,
                        param_8, (int*)abStack_1b8, iVar1, (void*)&DAT_806108c8, in_r6, in_r7, in_r8,
                        in_r9, in_r10);
            } else if (iVar1 < 2) {
                if (iVar1 == 0) {
                    piVar3 = (int*)get_string_by_id(*piVar8);
                    FUN_8038b510((int*)abStack_1b8, piVar3);
                } else if (-1 <= iVar1) {
                    iVar1 = hud_hint_get_command_field(piVar8[3], iVar5);
                    uVar12 = hud_format_button_prompt(dVar11, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar1, 0, (int)abStack_1f8, in_r6, in_r7, in_r8,
                            in_r9, in_r10);
                    iVar1 = get_string_by_id(*piVar8);
                    in_r6 = abStack_1f8;
                    format_string_varargs(uVar12, param_2, param_3, param_4, param_5, param_6, param_7,
                            param_8, (int*)abStack_1b8, iVar1, (void*)&DAT_806108c4, in_r6, in_r7, in_r8,
                            in_r9, in_r10);
                }
            } else if (iVar1 < 4) {
                iVar1 = hud_hint_get_command_field(piVar8[3], iVar5);
                uVar12 = hud_format_button_prompt(dVar11, param_2, param_3, param_4, param_5, param_6,
                        param_7, param_8, iVar1, 0, (int)abStack_1f8, in_r6, in_r7, in_r8, in_r9, in_r10);
                iVar1 = hud_hint_get_command_field(piVar8[4], iVar5);
                uVar12 = hud_format_button_prompt(uVar12, param_2, param_3, param_4, param_5, param_6,
                        param_7, param_8, iVar1, 0, (int)auStack_238, in_r6, in_r7, in_r8, in_r9, in_r10);
                iVar1 = hud_hint_get_command_field(piVar8[6], iVar5);
                uVar12 = hud_format_button_prompt(uVar12, param_2, param_3, param_4, param_5, param_6,
                        param_7, param_8, iVar1, 0, (int)auStack_278, in_r6, in_r7, in_r8, in_r9, in_r10);
                iVar1 = get_string_by_id(*piVar8);
                in_r6 = abStack_1f8;
                in_r7 = auStack_238;
                in_r8 = auStack_278;
                format_string_varargs(uVar12, param_2, param_3, param_4, param_5, param_6, param_7,
                        param_8, (int*)abStack_1b8, iVar1, (void*)&DAT_806108cc, in_r6, in_r7, in_r8,
                        in_r9, in_r10);
            }
            text_set_font_table(piVar8[1]);
            if (piVar8[6] < 0) {
                iVar1 = hud_hint_get_command_field(piVar8[3], iVar5);
                input_get_button_name(iVar1, (int*)abStack_1f8);
                pbVar4 = (unsigned char*)get_string_by_id(*piVar8);
                dVar11 = text_get_char_width(pbVar4);
                uVar9 = (unsigned int)((double)FLOAT_806157ac * (double)FLOAT_8060f958 + dVar11);
                local_38 = (long long)(int)uVar9;
                iVar1 = hud_hint_get_command_field(piVar8[3], iVar5);
                input_get_button_name(iVar1, (int*)abStack_1f8);
                dVar11 = text_get_char_width(abStack_1f8);
                uVar9 = (unsigned int)((double)(float)((double)(int)uVar9) + dVar11);
                param_2 = DOUBLE_80615738;
            } else {
                dVar11 = text_get_char_width(abStack_1b8);
                uVar9 = (unsigned int)dVar11;
                local_38 = (long long)(int)uVar9;
            }
            if ((int)uVar6 < (int)uVar9) {
                uVar6 = uVar9;
                double w = (double)FLOAT_806159f8;
                if ((double)(float)(w * (double)FLOAT_8060f958) < (double)(float)((double)(int)uVar9)) {
                    uVar6 = (unsigned int)(w * (double)FLOAT_8060f958);
                }
            }
            iVar10 = iVar10 + 0x1c;
        }
        iVar5 = (int)((float)((double)(int)uVar6) + FLOAT_806157d4);
        *(int*)(iVar2 + 0xec) = iVar5;
        (void)local_38;
    }
    FUN_80382730();
    return param_1;
}

// ----------------------------------------------------------------------------
// hud_hint_panel_begin_close (0x8012cea8)
// Set a hint panel's fade state machine to the closing state (3).
// param_1 is the record base offset (per-player index * 0x114).
// ----------------------------------------------------------------------------
void hud_hint_panel_begin_close(int param_1)
{
    if (param_1 < 0)
        return;
    if (*(int*)((int)&DAT_80582a38 + param_1 * 0x114) == 0)
        return;
    *(int*)((int)&DAT_80582a3c + param_1 * 0x114) = 3;
}

// ----------------------------------------------------------------------------
// hud_hint_panel_render (0x8012d1a8)
// Main per-frame draw of a hint panel: 9-slice bg + edge strip, per-line
// button-prompt formatting/draw, then a 4-state fade-in / hold / fade-out
// state machine (state at record +0xf8).
// ----------------------------------------------------------------------------
void hud_hint_panel_render(unsigned long long param_1, double param_2, double param_3, double param_4,
                           unsigned long long param_5, unsigned long long param_6,
                           unsigned long long param_7, unsigned long long param_8)
{
    int iVar5;
    unsigned int uVar7;
    int* piVar20;
    int iVar16;
    unsigned int uVar17;
    unsigned int uVar18;
    int iVar19;
    double dVar21;
    double dVar23;
    unsigned long long uVar22;
    unsigned char auStack_2b8[64];
    unsigned char auStack_278[64];
    unsigned char abStack_238[64];
    int aiStack_1f8[96];
    unsigned char* pbVar12;
    void* puVar13;
    void* puVar14;
    unsigned int in_r10 = 0;

    int iVar6 = FUN_803826d8();
    if (-1 <= iVar6) {
        int iVar11 = iVar6 * 0x114;
        if (*(int*)((int)&DAT_80582a38 + iVar11) != 0) {
            uVar18 = *(unsigned int*)((int)&DAT_80582a2c + iVar11);
            dVar21 = screen_effect_get_damage_alpha();
            dVar23 = (double)*(float*)((int)&DAT_80582a40 + iVar11);
            if (dVar21 <= dVar23)
                dVar23 = screen_effect_get_damage_alpha();
            int iVar15 = *(int*)((int)&DAT_80582a28 + iVar11);
            puVar14 = 0;
            FUN_8016f24c(8, iVar15 - 10, uVar18 - 5,
                    iVar15 + *(int*)((int)&DAT_80582a30 + iVar11) + 10,
                    uVar18 + *(int*)((int)&DAT_80582a34 + iVar11) + 5, 0);
            pbVar12 = (unsigned char*)(uVar18 + *(int*)((int)&DAT_80582a34 + iVar11) + 5);
            puVar13 = (void*)0x14;
            FUN_802e42c0(*(int*)((int)&DAT_80582a28 + iVar11) - 10, uVar18 - 5,
                    *(int*)((int)&DAT_80582a28 + iVar11) + *(int*)((int)&DAT_80582a30 + iVar11) + 10,
                    (unsigned int)pbVar12, 0x14);
            dVar21 = (double)(float)((double)FLOAT_806157c8 * dVar23);
            uVar7 = (unsigned int)FUN_803825c0(dVar21);
            iVar16 = 0;
            uVar17 = (uVar7 & 0xff) | 0x64646400;
            for (iVar19 = 0; iVar19 < *(int*)((int)&DAT_80582a24 + iVar11); iVar19 = iVar19 + 1) {
                piVar20 = (int*)((int)&DAT_80582944 + iVar16 + iVar11);
                iVar5 = piVar20[6];
                if (iVar5 == 2) {
                    iVar5 = hud_hint_get_command_field(piVar20[3], iVar6);
                    uVar22 = hud_format_button_prompt(dVar21, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar5, 0, (int)abStack_238, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                    iVar5 = hud_hint_get_command_field(piVar20[4], iVar6);
                    uVar22 = hud_format_button_prompt(uVar22, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar5, 0, (int)auStack_278, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                    iVar5 = get_string_by_id(*piVar20);
                    pbVar12 = abStack_238;
                    puVar13 = auStack_278;
                    format_string_varargs(uVar22, param_2, param_3, param_4, param_5, param_6,
                            param_7, param_8, aiStack_1f8, iVar5, (void*)&DAT_806108c8, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                } else if (iVar5 < 2) {
                    if (iVar5 == 0) {
                        int* piVar8 = (int*)get_string_by_id(*piVar20);
                        FUN_8038b510(aiStack_1f8, piVar8);
                    } else if (-1 <= iVar5) {
                        iVar5 = hud_hint_get_command_field(piVar20[3], iVar6);
                        uVar22 = hud_format_button_prompt(dVar21, param_2, param_3, param_4, param_5,
                                param_6, param_7, param_8, iVar5, 0, (int)abStack_238, pbVar12, puVar13,
                                puVar14, 0, in_r10);
                        iVar5 = get_string_by_id(*piVar20);
                        pbVar12 = abStack_238;
                        format_string_varargs(uVar22, param_2, param_3, param_4, param_5, param_6,
                                param_7, param_8, aiStack_1f8, iVar5, (void*)&DAT_806108c4, pbVar12,
                                puVar13, puVar14, 0, in_r10);
                    }
                } else if (iVar5 < 4) {
                    iVar5 = hud_hint_get_command_field(piVar20[3], iVar6);
                    uVar22 = hud_format_button_prompt(dVar21, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar5, 0, (int)abStack_238, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                    iVar5 = hud_hint_get_command_field(piVar20[4], iVar6);
                    uVar22 = hud_format_button_prompt(uVar22, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar5, 0, (int)auStack_278, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                    iVar5 = hud_hint_get_command_field(piVar20[6], iVar6);
                    uVar22 = hud_format_button_prompt(uVar22, param_2, param_3, param_4, param_5,
                            param_6, param_7, param_8, iVar5, 0, (int)auStack_2b8, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                    iVar5 = get_string_by_id(*piVar20);
                    pbVar12 = abStack_238;
                    puVar13 = auStack_278;
                    puVar14 = auStack_2b8;
                    format_string_varargs(uVar22, param_2, param_3, param_4, param_5, param_6,
                            param_7, param_8, aiStack_1f8, iVar5, (void*)&DAT_806108cc, pbVar12, puVar13,
                            puVar14, 0, in_r10);
                }
                *(int*)((int)&DAT_80582a28 + iVar11) = *(int*)((int)&DAT_80582a28 + iVar11) + 10;
                if (*piVar20 == 0x1789) {
                    text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718,
                            (double)FLOAT_80615868);
                    text_set_alpha(DOUBLE_80615a00);
                }
                text_set_font_table(piVar20[1]);
                if (piVar20[6] < 0) {
                    iVar5 = hud_hint_get_command_field(piVar20[3], iVar6);
                    input_get_button_name(iVar5, (int*)abStack_238);
                    double cx = (double)(int)*(int*)((int)&DAT_80582a28 + iVar11);
                    double cy = (double)(int)uVar18;
                    text_draw_char(cx, cy, abStack_238, uVar17);
                    void* scaled = text_check_scale(
                            -(double)(FLOAT_806157ac * FLOAT_8060f958 -
                                      (float)(double)(int)*(int*)((int)&DAT_80582a30 + iVar11)),
                            (void*)get_string_by_id(*piVar20));
                    text_draw_char((double)(float)((double)FLOAT_806157ac * (double)FLOAT_8060f958 + cx),
                            cy, scaled, uVar17);
                    dVar21 = text_get_char_height(scaled);
                    dVar21 = (double)(float)((double)FLOAT_80615788 + dVar21);
                    uVar18 = (unsigned int)(cy + dVar21);
                } else {
                    void* scaled = text_check_scale(
                            (double)(int)*(int*)((int)&DAT_80582a30 + iVar11),
                            (void*)aiStack_1f8);
                    double cx = (double)(int)*(int*)((int)&DAT_80582a28 + iVar11);
                    double cy = (double)(int)uVar18;
                    if (piVar20[2] == 0) {
                        text_draw_char(cx, cy, scaled, uVar17);
                        dVar21 = text_get_char_height(scaled);
                        dVar21 = (double)(float)((double)FLOAT_80615788 + dVar21);
                        uVar18 = (unsigned int)(cy + dVar21);
                    } else {
                        text_draw_char_shadowed(cx, cy, scaled, uVar17, uVar7 & 0xff);
                        dVar21 = text_get_char_height(scaled);
                        dVar21 = (double)(float)((double)FLOAT_80615760 + dVar21);
                        uVar18 = (unsigned int)(cy + dVar21);
                    }
                }
                *(int*)((int)&DAT_80582a28 + iVar11) = *(int*)((int)&DAT_80582a28 + iVar11) - 10;
                if (*piVar20 == 0x1789) {
                    text_set_color_rgb((double)FLOAT_80615718, (double)FLOAT_80615718,
                            (double)FLOAT_80615718);
                    text_set_alpha(DOUBLE_80615a08);
                }
                iVar16 = iVar16 + 0x1c;
            }
            *(unsigned int*)((int)&DAT_80582a34 + iVar11) =
                    uVar18 - *(int*)((int)&DAT_80582a2c + iVar11);

            float fVar4 = FLOAT_80615768;
            float fVar1 = FLOAT_80615760;
            float fVar3 = FLOAT_80615730;
            float fVar2 = FLOAT_80615718;
            iVar15 = *(int*)((int)&DAT_80582a3c + iVar11);
            if (iVar15 == 0) {
                if (FLOAT_80615884 <= *(float*)((int)&DAT_80582a44 + iVar11)) {
                    *(float*)((int)&DAT_80582a40 + iVar11) =
                            FLOAT_80615a10 * FLOAT_806108e8 + *(float*)((int)&DAT_80582a40 + iVar11);
                    if (fVar2 < *(float*)((int)&DAT_80582a40 + iVar11)) {
                        *(int*)((int)&DAT_80582a3c + iVar11) = 1;
                        *(float*)((int)&DAT_80582a40 + iVar11) = fVar2;
                        *(float*)((int)&DAT_80582a44 + iVar11) = FLOAT_80615730;
                    }
                } else {
                    *(float*)((int)&DAT_80582a44 + iVar11) =
                            FLOAT_806108e8 * (FLOAT_80615718 / (float)(double)(int)DAT_806109c0) +
                            *(float*)((int)&DAT_80582a44 + iVar11);
                }
            } else if (iVar15 == 1) {
                unsigned int mask = DAT_805015c0 & 0x40;
                *(float*)((int)&DAT_80582a44 + iVar11) =
                        FLOAT_806108e8 * (FLOAT_80615718 / (float)(double)(int)DAT_806109c0) +
                        *(float*)((int)&DAT_80582a44 + iVar11);
                float lim = FLOAT_80615780;
                if (mask == 0) {
                    int slot = DAT_80611d78 + iVar6 * 0x141c;
                    float d1 = *(float*)(slot + 0xc4) - *(float*)((int)&DAT_80582a50 + iVar11);
                    float d0 = *(float*)(slot + 0xc0) - *(float*)((int)&DAT_80582a4c + iVar11);
                    float d2 = *(float*)(slot + 200) - *(float*)((int)&DAT_80582a54 + iVar11);
                    *(float*)((int)&DAT_80582a48 + iVar11) =
                            *(float*)((int)&DAT_80582a48 + iVar11) +
                            FLOAT_80615748 * *(float*)(slot + 0x16c) + d2 * d2 + d0 * d0 + d1 * d1;
                    lim = fVar4;
                }
                if (lim < *(float*)((int)&DAT_80582a44 + iVar11)) {
                    *(float*)((int)&DAT_80582a44 + iVar11) = FLOAT_80615730;
                    *(int*)((int)&DAT_80582a3c + iVar11) = 2;
                }
            } else if (iVar15 == 2) {
                unsigned int mask = DAT_805015c0 & 0x40;
                *(float*)((int)&DAT_80582a44 + iVar11) =
                        FLOAT_806108e8 * (FLOAT_80615718 / (float)(double)(int)DAT_806109c0) +
                        *(float*)((int)&DAT_80582a44 + iVar11);
                float lim = fVar1;
                if (mask != 0)
                    lim = fVar2;
                if (*(float*)((int)&DAT_80582a44 + iVar11) <= lim) {
                    int slot = DAT_80611d78 + iVar6 * 0x141c;
                    float d1 = *(float*)(slot + 0xc4) - *(float*)((int)&DAT_80582a50 + iVar11);
                    float d0 = *(float*)(slot + 0xc0) - *(float*)((int)&DAT_80582a4c + iVar11);
                    float d2 = *(float*)(slot + 200) - *(float*)((int)&DAT_80582a54 + iVar11);
                    *(float*)((int)&DAT_80582a48 + iVar11) =
                            *(float*)((int)&DAT_80582a48 + iVar11) +
                            FLOAT_80615748 * *(float*)(slot + 0x16c) + d2 * d2 + d0 * d0 + d1 * d1;
                    if (FLOAT_806157f4 < *(float*)((int)&DAT_80582a48 + iVar11))
                        *(int*)((int)&DAT_80582a3c + iVar11) = 3;
                } else {
                    *(int*)((int)&DAT_80582a3c + iVar11) = 3;
                }
            } else {
                *(float*)((int)&DAT_80582a40 + iVar11) =
                        -(FLOAT_80615a14 * FLOAT_806108e8 - *(float*)((int)&DAT_80582a40 + iVar11));
                if (*(float*)((int)&DAT_80582a40 + iVar11) <= fVar3)
                    *(int*)((int)&DAT_80582a38 + iVar11) = 0;
            }
        }
    }
    FUN_80382724();
}

// ----------------------------------------------------------------------------
// hud_hint_show_objective_grab (0x8012cd0c)
// Shown-once (flag bit 0x4000) objective grab/action hint. Suppressed in
// mapmaker MP. Builds the hint via the queue then measures it.
// ----------------------------------------------------------------------------
void hud_hint_show_objective_grab(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8)
{
    long long lVar7 = (long long)FUN_803826ec();
    int iVar1 = (int)((unsigned long long)lVar7 >> 0x20);
    if ((-1 <= lVar7) && (game_is_mapmaker_multiplayer() == 0)) {
        int iVar3 = iVar1 * 0x114;
        double dVar6 = (double)(FLOAT_80615780 * FLOAT_806157e8 * FLOAT_80610830);
        int puVar4 = (int)&DAT_80582944 + iVar3;
        int obj = FUN_802448f8();
        if (obj != 0) {
            int state = *(int*)(obj + 0x7c);
            if (((&DAT_80582d94)[iVar1] & 0x4000) == 0) {
                (&DAT_80582d94)[iVar1] |= 0x4000;
                hud_hint_system_reset();
                hud_hint_begin(puVar4, DAT_8057ff4c + *DAT_80611748, (int)dVar6, 200, iVar1);
                hud_hint_add_command(puVar4, (int)lVar7, 3, 1);
                *(int*)((int)&DAT_80582a24 + iVar3) += 1;
                hud_hint_add_line(puVar4, 0x15b7, 0x1a, 0x12);
                hud_hint_add_line(puVar4, 0x15b8, 0x18, 0x12);
                if ((*(unsigned int*)(state + 0x24) & 1) == 0)
                    hud_hint_add_line(puVar4, 0x15fc, 0x15, 0x12);
                hud_hint_add_line(puVar4, 0x15fa, 0x14, 0x12);
                hud_hint_add_command(puVar4, 0x15cc, 3, 1);
                *(int*)((int)&DAT_80582a24 + iVar3) += 1;
                hud_hint_panel_measure(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
            }
        }
    }
    FUN_80382738();
}
