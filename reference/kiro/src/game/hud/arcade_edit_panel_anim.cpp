// Batch: batch_973 | Module: arcade/challenge editor animated slide-panel strip
// Address range: 0x80345484 - 0x80345de8
// Date: 2026-09-12
//
// The arcade/challenge editor's animated slide-panel (softkey/tab) strip. A fixed array of 10
// panel slots lives at DAT_805e2a90, each a 0x48-byte record (0x12-int stride). Slot layout:
//   +0x00 state        (0=idle, 1=animating-in, 2=animating-out)
//   +0x04..+0x20       eight geometry floats (two corner-pair rects: start + end position)
//   +0x2c anim         current animation phase accumulator
//   +0x30 animMax      phase ceiling
//   +0x34 t            eased interpolation parameter (0..1)
//   +0x24/+0x28        interpolated current X/Y (written each frame)
//   +0x38 alpha        interpolated alpha/scale
//   +0x3c active       computed visible flag
//   +0x40/+0x44        two edge-clip predicate flags
// Slot 9 is special: a scroll/drag thumb with its own state block at DAT_805e2d18/DAT_805e2d44.
// A separate hit-rect table at DAT_805e2de0 (0x10-byte rects) backs pointer hit-testing.
//
// The animation is an ease-in via sqrt(t): panel_slot_animate advances the phase by a
// frame-rate-scaled delta (FLOAT_806108e8 * anim-scale / framecount), computes t, and lerps the
// start/end corner pairs into the current X/Y. panel_slot_show kicks a slot into state 1;
// panel_slot_hide flips a shown slot to state 2 (retract); panel_tick_all advances all ten and
// recomputes the active flags each frame.
//
//   panel_slot_clear     0x80345484  zero the first 8 bytes (state + first geometry word) of a slot.
//   panel_slot_init      0x803454ac  store one slot's 8 geometry floats + reset anim/interp fields.
//   panel_layout_build   0x80345504  build all slot rects from the current screen/viewport params.
//   panel_slot_animate   0x80345938  advance one slot's ease timer and lerp its current position.
//   panel_tick_all       0x80345b88  tick all 10 slots and recompute their active flags.
//   panel_slot_show      0x80345c2c  begin showing a slot (state -> 1), seed its anim fields.
//   panel_scroll_reset   0x80345cd8  reset the slot-9 scroll thumb state.
//   panel_scroll_begin   0x80345cf4  begin a scroll drag (state 1/2), load the drag SFX + target.
//   panel_slot_hide      0x80345da8  begin hiding a shown slot (state 1 -> 2).
//   panel_hittest_point  0x80345de8  hit-test a screen point against the panel rects; slot index or -1.
//
// Callers: the arcade editor screen frame/render path and pointer-input dispatch.
//
// NON-MATCHING: reproduces the decompiler's structure. The panel array is addressed through the
// original &DAT_805e2a90 + idx*0x12 (int view) / +idx*0x48 (byte view) dual indexing, reproduced
// verbatim. The int->double magic-constant conversions (CONCAT44 with 0x43300000 minus
// DOUBLE_8061ac68) and the sqrt ease are kept as-is. Paired-single spills are expressed as plain
// double parameters (Compile-Clean rule 3). Raw FUN_/DAT_/FLOAT_ symbols are declared file-locally
// with call-site-accurate arity (rule 5); named helpers resolve through the shared headers (rule 6).
// CONCAT44 comes from common.h.

#include "common.h"

extern "C" {

// ---- panel slot array + special slot-9 scroll block ------------------------
extern int    DAT_805e2a90;   // panel slot array base: [10] x 0x48 (state at +0x00)
extern float  DAT_805e2a94;   // slot +0x04 : geometry float 0
extern float  DAT_805e2a98;   // slot +0x08 : geometry float 1
extern float  DAT_805e2a9c;   // slot +0x0c : geometry float 2 (start X)
extern float  DAT_805e2aa0;   // slot +0x10 : geometry float 3 (start Y)
extern float  DAT_805e2aa4;   // slot +0x14 : geometry float 4 (end X)
extern float  DAT_805e2aa8;   // slot +0x18 : geometry float 5 (end Y)
extern float  DAT_805e2aac;   // slot +0x1c : geometry float 6
extern float  DAT_805e2ab0;   // slot +0x20 : geometry float 7
extern float  DAT_805e2ab4;   // slot +0x24 : interpolated current X
extern float  DAT_805e2ab8;   // slot +0x28 : interpolated current Y
extern float  DAT_805e2abc;   // slot +0x2c : anim phase accumulator
extern float  DAT_805e2ac0;   // slot +0x30 : anim phase ceiling
extern float  DAT_805e2ac4;   // slot +0x34 : eased t
extern float  DAT_805e2ac8;   // slot +0x38 : interpolated alpha/scale
extern int    DAT_805e2acc;   // slot +0x3c : active flag
extern int    DAT_805e2ad0;   // slot +0x40 : edge-clip predicate 0
extern int    DAT_805e2ad4;   // slot +0x44 : edge-clip predicate 1

extern int    DAT_805e2d18;   // scroll thumb state (0/1/2)
extern unsigned int uRam805e2d24; // scroll thumb src X snapshot
extern unsigned int uRam805e2d28; // scroll thumb src Y snapshot
extern int    DAT_805e2d34;   // scroll thumb target position
extern unsigned int uRam805e2d3c; // scroll thumb cur X
extern unsigned int uRam805e2d40; // scroll thumb cur Y
extern float  DAT_805e2d44;   // scroll thumb phase accumulator
extern float  DAT_805e2d48;   // scroll thumb phase ceiling
extern float  fRam805e2d50;   // scroll thumb normalized t

extern float  DAT_805e2de0;   // hit-rect table base: [n] x 0x10 (minX)
extern float  DAT_805e2de4;   // hit-rect +0x04 : minY
extern float  DAT_805e2de8;   // hit-rect +0x08 : maxX
extern float  DAT_805e2dec;   // hit-rect +0x0c : maxY

extern int    DAT_80612be4;   // editor context base (mode flag at +0x200)
extern int    DAT_80612d70;   // panel-strip visible flag
extern int    DAT_80612d74;   // scroll drag target A
extern int    DAT_80612d78;   // scroll drag target B
extern float  FLOAT_80612d7c; // panel-strip global timer
extern int    DAT_8060f948;   // screen-param handle

// ---- screen/viewport layout params -----------------------------------------
extern int    DAT_806109c0;   // frame-count denominator (state 9)
extern int    DAT_806109c4;   // screen extent X (negated int->float)
extern int    DAT_806109c8;   // screen extent Y (negated int->float)
extern double DOUBLE_8061ac68; // int->double magic bias
extern float  FLOAT_806108e8;  // per-frame anim rate
extern float  FLOAT_80611a64;  // viewport scale A
extern float  FLOAT_80611a68;  // viewport scale B
extern float  FLOAT_80611a6c;  // viewport bias A
extern float  FLOAT_80611a70;  // viewport bias B

// ---- panel layout constants (FLOAT_8061ac38.. block) -----------------------
extern float  FLOAT_8061ac38;  // 0.0f
extern float  FLOAT_8061ac3c;  // 1.0f
extern float  FLOAT_8061ac40, FLOAT_8061ac44, FLOAT_8061ac48, FLOAT_8061ac4c;
extern float  FLOAT_8061ac50, FLOAT_8061ac54, FLOAT_8061ac58, FLOAT_8061ac5c;
extern float  FLOAT_8061ac60, FLOAT_8061ac64;
extern float  FLOAT_8061ac70;  // slot-9 phase ceiling / alpha rate
extern float  FLOAT_8061ac74;  // state-2 alpha scale
extern float  FLOAT_8061ac78;  // show seed phase
extern float  FLOAT_8061ac7c;  // show seed ceiling
extern float  FLOAT_8061ac80;  // hide seed ceiling

// ---- named cross-module helpers --------------------------------------------
extern void   memset_wrapper(int dst, int val, int len);
extern double math_sqrt(double x);
extern double aveditor_screen_param_x(int handle);
extern unsigned long long FUN_803826f0(void);   // push scope (returns record)
extern void   FUN_8038273c(void);                // pop scope
extern void   FUN_8038b4cc(int a, int b, int c); // load/play drag SFX
extern unsigned long long __save_gpr_28(void);
extern void   __restore_gpr_28(void);

// ---- forward decls within this TU ------------------------------------------
void panel_slot_init(double p1, double p2, double p3, double p4, double p5,
                     double p6, double p7, double p8, int idx);
void panel_slot_animate(int idx);

// ---------------------------------------------------------------------------
// 0x80345484 : zero the first 8 bytes (state + first geometry word) of a slot
// ---------------------------------------------------------------------------
void panel_slot_clear(int slotBase)
{
    memset_wrapper(slotBase, 0, 8);
}

// ---------------------------------------------------------------------------
// 0x803454ac : store one slot's 8 geometry floats + reset anim/interp fields
// ---------------------------------------------------------------------------
void panel_slot_init(double p1, double p2, double p3, double p4, double p5,
                     double p6, double p7, double p8, int idx)
{
    float seedCeil = FLOAT_8061ac3c;
    float seedZero = FLOAT_8061ac38;
    int   off = idx * 0x48;

    (&DAT_805e2a90)[idx * 0x12] = 0;
    *(float*)(&DAT_805e2a94 + off) = (float)p1;
    *(float*)(&DAT_805e2a98 + off) = (float)p2;
    *(float*)(&DAT_805e2a9c + off) = (float)p3;
    *(float*)(&DAT_805e2aa0 + off) = (float)p4;
    *(float*)(&DAT_805e2aa4 + off) = (float)p5;
    *(float*)(&DAT_805e2aa8 + off) = (float)p6;
    *(float*)(&DAT_805e2aac + off) = (float)p7;
    *(float*)(&DAT_805e2ab0 + off) = (float)p8;
    *(int*)(&DAT_805e2ad0 + off) = 0;
    *(int*)(&DAT_805e2ad4 + off) = 0;
    (&DAT_805e2acc)[idx * 0x12] = 0;
    (&DAT_805e2abc)[idx * 0x12] = (int)seedZero;
    *(float*)(&DAT_805e2ac0 + off) = seedCeil;
}

// ---------------------------------------------------------------------------
// 0x80345504 : build all slot rects from the current screen/viewport params
// ---------------------------------------------------------------------------
void panel_layout_build(void)
{
    float f1;
    double d3, d9, d8, d11, d2, d10, d7, d6, d5, d4;

    d3 = (double)(FLOAT_8061ac40 * FLOAT_80611a64);
    d9 = -d3;
    f1 = FLOAT_8061ac48 * FLOAT_80611a64 + FLOAT_80611a6c;
    d8 = (double)(FLOAT_8061ac44 * FLOAT_80611a64 + f1);
    d11 = (double)(FLOAT_8061ac4c * FLOAT_80611a68 + FLOAT_80611a70);
    d2 = (double)(float)((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                         DOUBLE_8061ac68) - d11);
    d10 = (double)(FLOAT_8061ac50 * FLOAT_80611a68 + FLOAT_80611a70);
    d7 = (double)(float)((double)((float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) -
                                          DOUBLE_8061ac68) - f1) - d3);
    d6 = (double)(FLOAT_8061ac54 * FLOAT_80611a68 + FLOAT_80611a70);
    d5 = (double)(FLOAT_8061ac58 * FLOAT_80611a68 + FLOAT_80611a70);
    d4 = (double)(FLOAT_8061ac5c * FLOAT_80611a68 + FLOAT_80611a70);

    panel_slot_init((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d8, d2, d8, d2, d9, d11, d3, 0);
    d2 = (double)(float)((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                         DOUBLE_8061ac68) - d11);
    panel_slot_init((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d7, d2, d7, d2,
                    (double)(float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d11, d3, 1);
    panel_slot_init(-d10, d8, (double)FLOAT_8061ac38, d8, (double)FLOAT_8061ac38, d9, d10, d3, 2);
    d2 = (double)(float)((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                         DOUBLE_8061ac68) - d4);
    panel_slot_init((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d8, d2, d8, d2, d9, d4, d3, 3);
    d2 = (double)(float)((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                         DOUBLE_8061ac68) - d6);
    panel_slot_init((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d8, d2, d8, d2, d9, d6, d3, 4);
    panel_slot_init(-d5, d7, (double)FLOAT_8061ac38, d7, (double)FLOAT_8061ac38,
                    (double)(float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d5, d3, 5);
    d2 = (double)FLOAT_8061ac38;
    panel_slot_init(d2, d7, d2, d7, d2, d7, d10, d3, 9);
    d2 = (double)(float)((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                         DOUBLE_8061ac68) - d6);
    panel_slot_init((double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                    DOUBLE_8061ac68),
                    d8, d2, d8, d2, d9, d6, d3, 6);
    d2 = -(double)(float)((double)FLOAT_8061ac60 * (double)FLOAT_80611a68 - d7);
    d4 = (double)(float)((double)FLOAT_8061ac54 * (double)FLOAT_80611a68);
    panel_slot_init(-d4, d2, (double)FLOAT_8061ac38, d2, (double)FLOAT_8061ac38, d9, d4, d3, 7);
    d2 = aveditor_screen_param_x(DAT_8060f948);
    d5 = -(double)(float)((double)FLOAT_80611a68 * d2 -
                          (double)(float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) -
                                          DOUBLE_8061ac68));
    d2 = aveditor_screen_param_x(DAT_8060f948);
    d4 = (double)FLOAT_8061ac64;
    panel_slot_init(-(double)(float)((double)FLOAT_80611a68 * d2 -
                                     (double)(float)((double)CONCAT44(0x43300000,
                                                                      DAT_806109c4 ^ 0x80000000) -
                                                     DOUBLE_8061ac68)),
                    d4, (double)FLOAT_8061ac38, d4, d5, d9, d4, d3, 8);
    DAT_80612d70 = 0;
    FLOAT_80612d7c = FLOAT_8061ac38;
}

// ---------------------------------------------------------------------------
// 0x80345938 : advance one slot's ease timer and lerp its current position
// ---------------------------------------------------------------------------
void panel_slot_animate(int idx)
{
    unsigned int uv2;
    int off;
    int* state;
    double d5, d6;
    float delta;

    off = idx * 0x48;
    state = &DAT_805e2a90 + idx * 0x12;
    delta = FLOAT_806108e8 *
            (FLOAT_8061ac3c /
             (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061ac68));
    if (idx == 9) {
        if (DAT_805e2d18 == 1) {
            DAT_805e2d44 = DAT_805e2d44 + delta;
            if (DAT_805e2d48 < DAT_805e2d44) {
                DAT_805e2d44 = DAT_805e2d48;
            }
        }
        else {
            DAT_805e2d44 = DAT_805e2d44 - delta;
            if (DAT_805e2d44 < FLOAT_8061ac38) {
                DAT_805e2d44 = FLOAT_8061ac38;
                DAT_805e2d18 = 0;
            }
        }
        uRam805e2d3c = uRam805e2d24;
        uRam805e2d40 = uRam805e2d28;
        fRam805e2d50 = DAT_805e2d44 / DAT_805e2d48;
    }
    else {
        (&DAT_805e2abc)[idx * 0x12] = (float)(&DAT_805e2abc)[idx * 0x12] + delta;
        if ((*(float*)(&DAT_805e2ac0 + off) <= (float)(&DAT_805e2abc)[idx * 0x12]) &&
            ((&DAT_805e2abc)[idx * 0x12] = (int)*(float*)(&DAT_805e2ac0 + off),
             delta = FLOAT_8061ac38, *state == 2)) {
            *state = 0;
            *(float*)(&DAT_805e2ac4 + off) = delta;
        }
        if (*state == 1) {
            if (FLOAT_8061ac38 < (float)(&DAT_805e2abc)[idx * 0x12]) {
                *(float*)(&DAT_805e2ac4 + off) =
                    (float)(&DAT_805e2abc)[idx * 0x12] / *(float*)(&DAT_805e2ac0 + off);
            }
            else {
                *(float*)(&DAT_805e2ac4 + off) = FLOAT_8061ac38;
            }
            d5 = math_sqrt((double)*(float*)(&DAT_805e2ac4 + off));
            delta = FLOAT_8061ac70;
            *(float*)(&DAT_805e2ab4 + off) =
                (float)(d5 * (double)(float)((double)*(float*)(&DAT_805e2a9c + off) -
                                             (double)*(float*)(&DAT_805e2a94 + off)) +
                        (double)*(float*)(&DAT_805e2a94 + off));
            *(float*)(&DAT_805e2ab8 + off) =
                (float)(d5 * (double)(float)((double)*(float*)(&DAT_805e2aa0 + off) -
                                             (double)*(float*)(&DAT_805e2a98 + off)) +
                        (double)*(float*)(&DAT_805e2a98 + off));
            *(float*)(&DAT_805e2ac8 + off) = delta * *(float*)(&DAT_805e2ac4 + off) + delta;
        }
        else if (*state == 2) {
            *(float*)(&DAT_805e2ac4 + off) =
                FLOAT_8061ac3c -
                (float)(&DAT_805e2abc)[idx * 0x12] / *(float*)(&DAT_805e2ac0 + off);
            d6 = math_sqrt((double)*(float*)(&DAT_805e2ac4 + off));
            d5 = (double)FLOAT_8061ac74;
            *(float*)(&DAT_805e2ab4 + off) =
                (float)(d6 * (double)(float)((double)*(float*)(&DAT_805e2a9c + off) -
                                             (double)*(float*)(&DAT_805e2aa4 + off)) +
                        (double)*(float*)(&DAT_805e2aa4 + off));
            *(float*)(&DAT_805e2ab8 + off) =
                (float)(d6 * (double)(float)((double)*(float*)(&DAT_805e2aa0 + off) -
                                             (double)*(float*)(&DAT_805e2aa8 + off)) +
                        (double)*(float*)(&DAT_805e2aa8 + off));
            *(float*)(&DAT_805e2ac8 + off) = (float)(d5 * d6);
        }
    }
    uv2 = DAT_806109c4;
    *(unsigned int*)(&DAT_805e2ad0 + off) =
        (unsigned int)(FLOAT_8061ac38 < *(float*)(&DAT_805e2a9c + off));
    *(unsigned int*)(&DAT_805e2ad4 + off) =
        (unsigned int)(*(float*)(&DAT_805e2a9c + off) + *(float*)(&DAT_805e2aac + off) <
                       (float)((double)CONCAT44(0x43300000, uv2 ^ 0x80000000) - DOUBLE_8061ac68));
}

// ---------------------------------------------------------------------------
// 0x80345b88 : tick all 10 slots and recompute their active flags
// ---------------------------------------------------------------------------
void panel_tick_all(void)
{
    int active;
    int state;
    int idx;
    int off;
    double zero;

    __save_gpr_28();
    zero = (double)FLOAT_8061ac38;
    idx = 0;
    off = 0;
    do {
        if (*(int*)((int)&DAT_805e2a90 + off) != 0) {
            panel_slot_animate(idx);
        }
        state = *(int*)((int)&DAT_805e2a90 + off);
        active = 0;
        if ((state != 0) &&
            ((state != 1) || (zero <= (double)*(float*)((int)&DAT_805e2abc + off)))) {
            active = 1;
        }
        idx = idx + 1;
        *(int*)((int)&DAT_805e2acc + off) = active;
        off = off + 0x48;
    } while (idx < 10);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80345c2c : begin showing a slot (state -> 1), seed its anim fields
// ---------------------------------------------------------------------------
void panel_slot_show(int idx)
{
    float seedCeil = FLOAT_8061ac7c;
    float one = FLOAT_8061ac3c;
    float zero = FLOAT_8061ac38;
    int   off = idx * 0x48;
    int   state = (&DAT_805e2a90)[idx * 0x12];

    if (state == 0) {
        (&DAT_805e2abc)[idx * 0x12] = (int)FLOAT_8061ac78;
        *(float*)(&DAT_805e2ac0 + off) = seedCeil;
        *(float*)(&DAT_805e2ac4 + off) = zero;
    }
    else if ((state != 2) || (FLOAT_8061ac7c <= *(float*)(&DAT_805e2ac4 + off))) {
        if (state == 2) {
            *(float*)(&DAT_805e2ac0 + off) = FLOAT_8061ac7c;
            (&DAT_805e2abc)[idx * 0x12] = *(int*)(&DAT_805e2ac0 + off);
            *(float*)(&DAT_805e2ac4 + off) = one;
        }
    }
    else {
        (&DAT_805e2abc)[idx * 0x12] = (int)FLOAT_8061ac38;
        *(float*)(&DAT_805e2ac0 + off) = seedCeil;
        *(float*)(&DAT_805e2ac4 + off) = zero;
    }
    (&DAT_805e2a90)[idx * 0x12] = 1;
    panel_slot_animate(idx);
}

// ---------------------------------------------------------------------------
// 0x80345cd8 : reset the slot-9 scroll thumb state
// ---------------------------------------------------------------------------
void panel_scroll_reset(void)
{
    DAT_805e2d18 = 0;
    DAT_805e2d44 = FLOAT_8061ac38;
}

// ---------------------------------------------------------------------------
// 0x80345cf4 : begin a scroll drag (state 1/2), load the drag SFX + target
// ---------------------------------------------------------------------------
void panel_scroll_begin(unsigned int a, unsigned int b, int targetA, unsigned int targetB)
{
    int rec;
    (void)a; (void)b;

    rec = (int)FUN_803826f0();
    if (rec == 0) {
        DAT_805e2d18 = 2;
    }
    else {
        DAT_805e2d18 = 1;
        DAT_805e2d48 = FLOAT_8061ac70;
        FUN_8038b4cc(-0x7fa1d2a0, rec, 0x7f);
        DAT_80612d74 = targetA;
        DAT_80612d78 = (int)targetB;
        if (targetA == -1) {
            DAT_805e2d34 = FLOAT_8061ac50 * FLOAT_80611a68 + FLOAT_80611a70;
        }
        else {
            DAT_805e2d34 = FLOAT_8061ac4c * FLOAT_80611a68 + FLOAT_80611a70;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80345da8 : begin hiding a shown slot (state 1 -> 2)
// ---------------------------------------------------------------------------
void panel_slot_hide(int idx)
{
    float hideCeil = FLOAT_8061ac80;
    float one;

    if ((&DAT_805e2a90)[idx * 0x12] != 1) {
        return;
    }
    (&DAT_805e2abc)[idx * 0x12] = (int)FLOAT_8061ac38;
    one = FLOAT_8061ac3c;
    *(float*)(&DAT_805e2ac0 + idx * 0x48) = hideCeil;
    *(float*)(&DAT_805e2ac4 + idx * 0x48) = one;
    (&DAT_805e2a90)[idx * 0x12] = 2;
}

// ---------------------------------------------------------------------------
// 0x80345de8 : hit-test a screen point against the panel rects; slot index or -1
// ---------------------------------------------------------------------------
int panel_hittest_point(double px, double py)
{
    int count;
    int idx;
    int off;

    count = 3;
    if (*(int*)(DAT_80612be4 + 0x200) != 0) {
        count = 5;
    }
    if (DAT_80612d70 != 0) {
        idx = 0;
        off = 0;
        for (; count != 0; count = count + -1) {
            if ((((double)*(float*)((int)&DAT_805e2de0 + off) <= px) &&
                 (px <= (double)*(float*)((int)&DAT_805e2de8 + off))) &&
                (((double)*(float*)((int)&DAT_805e2de4 + off) <= py) &&
                 (py <= (double)*(float*)((int)&DAT_805e2dec + off)))) {
                return idx;
            }
            idx = idx + 1;
            off = off + 0x10;
        }
    }
    return -1;
}

} // extern "C"
