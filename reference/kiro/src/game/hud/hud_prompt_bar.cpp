// Batch 342 | Date: 2026-09-04
// HUD context-prompt bar: the horizontal row of "button = action" prompts shown
// at the top of the player view (up to 0x12 = 18 slots). Each slot has an
// animated fade/expand value stored in the per-view prompt table (DAT_805824c4,
// 0x120-byte stride per view, 0x10 bytes per slot). This group covers:
//
//   0x80122e9c hud_prompt_bar_charge_slot   (bump one slot's fade toward full)
//   0x80122f64 hud_prompt_bar_fade_tick     (per-frame fade of all 18 slots)
//   0x80123048 hud_prompt_bar_render        (MASSIVE: draw all button+label rows)
//   0x801233dc hud_prompt_objective_hint    (objective/carrier action hint line)
//   0x80123ecc hud_prompt_slide_tick        (per-frame slide/expand animation)
//
// The render resolves each prompt's controller glyph (input_get_button_name or,
// for negative pseudo-buttons, a fixed template) plus its localized label
// (get_string_by_id + FUN_8038b510) and draws them with text_draw_char_shadowed.
// Positions derive from the viewport rect (DAT_80611748) and layout insets.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies; every raw
// FUN_/DAT_/FLOAT_/DOUBLE_ symbol is declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// scalar float / double constants
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_8061576c;
extern float  FLOAT_8061579c;
extern float  FLOAT_806157c0;
extern float  FLOAT_806157c4;
extern float  FLOAT_806157cc;
extern float  FLOAT_806157d4;
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615750;   // 255.0
extern float  FLOAT_8061572c;
extern float  FLOAT_80615768;
extern float  FLOAT_80615788;
extern float  FLOAT_806157b0;
extern float  FLOAT_8061582c;
extern float  FLOAT_80615838;
extern float  FLOAT_80615864;
extern float  FLOAT_80615814;
extern float  FLOAT_80615884;
extern float  FLOAT_806108e8;
extern float  FLOAT_806108ec;
extern float  FLOAT_8061082c;
extern float  FLOAT_80610830;
extern float  FLOAT_80611674;
extern double DOUBLE_80615738; // uint->double bias (0x43300000_00000000)

// data / globals (arrays accessed via &NAME + arithmetic are declared as
// scalars per the tree's convention so &NAME yields a same-size pointer)
extern int    DAT_8040d258;     // prompt-slot -> button-id table (18 entries)
extern int    DAT_805824c4;     // per-view prompt state table (0x120 stride, slot 0x10)
extern int    DAT_80611d74;     // active HUD view base
extern int*   DAT_80611748;     // viewport rect
extern int    DAT_806109c0;     // frame delta accumulator (as uint magnitude)
extern int    DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58; // layout insets
extern int    DAT_80610838;     // default HUD font table
extern uint   DAT_805015c0;     // mode flags (bit 0x40 = bottom-anchored)
extern int    DAT_80611928;     // frame counter
extern int    DAT_80611934;     // current game phase
extern int    DAT_805015d8;     // challenge/objective id
extern unsigned char DAT_805015bd; // game mode byte
extern int    DAT_80612610;     // active player count
extern int    DAT_80611d78;     // player-slot table base
extern float  DAT_80583228;     // sine LUT
extern int    DAT_806108a4;     // pseudo-button glyph template (composed via FUN_8038b510)
extern unsigned char DAT_80578684;   // per-view HUD scale table (0x177c stride)
extern int    DAT_80579dd0;     // secondary prompt-anim state table (0x177c stride): state word
extern float  DAT_80579dd4;     //   +0x04: hold timer
extern float  DAT_80579dd8;     //   +0x08: width/expand value
extern float  DAT_80579ddc;     //   +0x0c: horizontal offset (current)
extern float  DAT_80579de0;     //   +0x10: horizontal offset (target)
extern float  DAT_80579de4;     //   +0x14: highlight ramp

// named helpers (raw, declared per-file per Compile-Clean Standard rule 5)
extern void   FUN_803826d8(void);            // HUD push (prompt bar)
extern void   FUN_80382724(void);            // HUD pop  (prompt bar)
extern void   FUN_803826e4(void);            // HUD push (objective hint)
extern void   FUN_80382730(void);            // HUD pop  (objective hint)
extern int    text_set_font_table(int table);
extern int    get_string_by_id(int id);
extern void   input_get_button_name(int button, int* out_glyph);
extern void   FUN_8038b510(int* dst_glyph, int* src);
extern void   text_draw_char_shadowed(double x, double y, unsigned char* glyph, uint color, uint shadow);
extern double text_get_char_width(unsigned char* glyph);
extern double text_get_char_height(unsigned char* glyph);
extern double screen_effect_get_damage_alpha(void);
extern void   FUN_8016f00c(int mode);
extern uint   FUN_80132348(double a, uint color);   // fade color by damage alpha
extern int    FUN_803825c0(double v);               // float -> clamped byte
extern void   FUN_801236f0(double p1, double p2);   // reticle charge-meter draw (hud_reticle.cpp neighbour)

// forward decls (this file)
extern "C" {
void hud_prompt_bar_charge_slot(int view, int buttonId, float stamp);
void hud_prompt_bar_fade_tick(void);
void hud_prompt_bar_render(void);
void hud_prompt_objective_hint(void);
void hud_prompt_slide_tick(void);
}

// ============================================================================
// 0x80122e9c | hud_prompt_bar_charge_slot
// Finds the prompt slot for buttonId in DAT_8040d258 (18 entries); if that slot
// is active (fade > 0) and not already stamped for this frame (stamp), pushes
// its fade toward full at a frame-delta-scaled rate and records the stamp.
// ============================================================================
extern "C" void hud_prompt_bar_charge_slot(int view, int buttonId, float stamp) {
    double bias = DOUBLE_80615738;
    float one = FLOAT_80615718;
    int idx = 0;
    int off = 0;
    int remain = 0x12;
    while (buttonId != *(int*)((int)&DAT_8040d258 + off)) {
        idx++;
        off += 4;
        remain--;
        if (remain == 0) return;
    }
    float* slot = (float*)(&DAT_805824c4 + view * 0x48 + idx * 4);
    if ((FLOAT_80615730 != slot[0]) && (slot[3] != stamp)) return;

    uint mag = (uint)DAT_806109c0 ^ 0x80000000;
    slot[1] = FLOAT_8061579c;
    float v = slot[1] * (one / (float)((double)CONCAT44(0x43300000, mag) - bias));
    if (v < slot[0]) v = slot[0];
    slot[0] = v;
    slot[3] = stamp;
    return;
}

// ============================================================================
// 0x80122f64 | hud_prompt_bar_fade_tick
// Per-frame update of all 18 prompt slots for the active view: slots at/below
// the floor snap to the resting value; active slots integrate their fade by the
// frame delta, clamped to [rest, 1.0].
// ============================================================================
extern "C" void hud_prompt_bar_fade_tick(void) {
    float rest = FLOAT_80615730;
    int viewBase = DAT_80611d74;
    float delta = FLOAT_806108ec *
                  (FLOAT_80615718 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738));
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        int off = 0;
        int remain = 0x12;
        do {
            float* slot = (float*)((int)&DAT_805824c4 + off + *(int*)(viewBase + 4) * 0x120);
            if (slot[0] <= rest) {
                slot[2] = FLOAT_80615838;
                slot[1] = rest;
            } else {
                float next = slot[1] * delta + slot[0];
                float clampHi = next;
                if (FLOAT_80615718 < next) clampHi = FLOAT_80615718;
                float result = FLOAT_80615730;
                if ((rest <= clampHi) && (result = next, FLOAT_80615718 < next)) {
                    result = FLOAT_80615718;
                }
                slot[0] = result;
                slot[1] = FLOAT_80615864;
            }
            off += 0x10;
            remain--;
        } while (remain != 0);
    }
    return;
}

// ============================================================================
// 0x80123048 | hud_prompt_bar_render  (MASSIVE)
// Draws the full context-prompt bar for the active view: for each of the 18
// slots with a positive fade, animates its Y toward the target row, resolves the
// controller glyph (input_get_button_name, or a fixed template for negative
// pseudo-buttons) and the localized label, and draws both shadowed. The bar
// anchors to the top of the viewport, or the bottom when the mode flag 0x40 is
// set. In game phase 5 with objective id != 6 the whole bar is suppressed.
// ============================================================================
extern "C" void hud_prompt_bar_render(void) {
    unsigned char glyph[256];
    double xCursor;
    double rowY;

    FUN_803826d8();
    uint topMag = DAT_8057ff50 + DAT_80611748[5] ^ 0x80000000;
    double leftD = (double)CONCAT44(0x43300000, DAT_8057ff4c + *DAT_80611748 ^ 0x80000000);
    rowY = (double)(FLOAT_806157d4 + (float)((double)CONCAT44(0x43300000, topMag) - DOUBLE_80615738));
    xCursor = (double)(FLOAT_806157d4 + (float)(leftD - DOUBLE_80615738));

    int savedFont = text_set_font_table(DAT_80610838);
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        if ((DAT_805015c0 & 0x40) != 0) {
            double botMag = (double)(CONCAT44(0x43300000, DAT_80611748[6] - DAT_8057ff58) ^ 0x80000000);
            rowY = (double)(((float)(botMag - DOUBLE_80615738) - FLOAT_80615768) - FLOAT_80611674);
            if ((DAT_80611934 == 5) && (DAT_805015d8 != 6)) {
                text_set_font_table(savedFont);
                FUN_80382724();
                return;
            }
        }
        double rest = (double)FLOAT_80615730;
        int slotIdx = 0;
        int btnOff = 0;
        int slotOff = 0;
        do {
            float* slot = (float*)((int)&DAT_805824c4 + slotOff + *(int*)(DAT_80611d74 + 4) * 0x120);
            if (rest < (double)slot[0]) {
                double curY = (double)slot[2];
                double dmag = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
                double step = (double)((FLOAT_80615718 / (float)(dmag - DOUBLE_80615738)) *
                                       FLOAT_806108ec * FLOAT_806157b0 *
                                       (float)((uint)(float)(rowY - curY) & 0x80000000 | 0x3f800000));
                if ((curY < rest) || (ABS((double)(float)(rowY - curY)) <= ABS(step))) {
                    slot[2] = (float)rowY;
                } else {
                    slot[2] = (float)(curY + step);
                }
                int btnId = *(int*)((int)&DAT_8040d258 + btnOff);
                uint intensity = (int)(FLOAT_80615750 * slot[0]) & 0xff;
                uint colorA = intensity | 0x64646400;
                uint colorB = intensity | 0x1e1e1e00;
                if (btnId < 0) {
                    btnId = -btnId;
                    if (btnId < 3) {
                        if (0 < btnId) FUN_8038b510((int*)glyph, (int*)&DAT_806108a4);
                    } else if (btnId < 5) {
                        FUN_8038b510((int*)glyph, (int*)&DAT_806108a4);
                    }
                } else {
                    input_get_button_name(btnId, (int*)glyph);
                }
                double glyphY = (double)slot[2];
                text_draw_char_shadowed(xCursor, glyphY, glyph, colorA, colorB);
                double gw = text_get_char_width(glyph);
                xCursor = (double)((float)(xCursor + gw) + FLOAT_806157d4);

                int* label = (int*)get_string_by_id((int)slot[3]);
                FUN_8038b510((int*)glyph, label);
                text_draw_char_shadowed(xCursor, glyphY, glyph, colorA, colorB);
                text_get_char_width(glyph);
                double gh = text_get_char_height(glyph);

                double lD = (double)(CONCAT44(0x43300000, DAT_8057ff4c + *DAT_80611748) ^ 0x80000000);
                xCursor = (double)(FLOAT_806157d4 + (float)(lD - DOUBLE_80615738));
                if ((DAT_805015c0 & 0x40) == 0) {
                    rowY = (double)((float)(glyphY + gh) + FLOAT_80615748 + FLOAT_80611674);
                } else {
                    rowY = (double)((float)(glyphY + gh) - (FLOAT_80615748 + FLOAT_80611674));
                }
            }
            slotIdx++;
            slotOff += 0x10;
            btnOff += 4;
        } while (slotIdx < 0x12);
        text_set_font_table(savedFont);
    }
    FUN_80382724();
    return;
}

// ============================================================================
// 0x801233dc | hud_prompt_objective_hint
// Draws the single objective/carrier action-hint line (e.g. "throw" / "return"),
// scanning the 6-entry objective table (0xec8 stride) for an active state-3
// carrier. Feeds the carrier's per-slot fade into the reticle charge meter
// (FUN_801236f0), then draws the localized hint (string 0x15e3 with a pulsing
// color, or 0x15e4 for the neutral case) right-aligned into the viewport.
// ============================================================================
extern "C" void hud_prompt_objective_hint(void) {
    FUN_803826e4();
    double topD = (double)(CONCAT44(0x43300000, DAT_8057ff50 + *(int*)(DAT_80611748 + 0x14)) ^ 0x80000000);
    uint rightMag = *(int*)(DAT_80611748 + 4) - DAT_8057ff54;
    uint yMag = (uint)(FLOAT_8061582c * FLOAT_80610830 + (float)(topD - DOUBLE_80615738));
    double dmgAlpha = screen_effect_get_damage_alpha();
    text_set_font_table(DAT_80610838);

    int tableOff = 0;
    int viewIdx = 0;
    int found = -1;
    int carrierState = 0;
    for (int e = 6; e != 0; e--) {
        if (*(int*)(DAT_80611d74 + tableOff + 0xec8) != 0) {
            int count = *(int*)(DAT_80611d74 + tableOff + 0xedc);
            int entOff = 0;
            found = viewIdx;
            carrierState = 0;
            if (0 < count) {
                for (int i = count; i != 0; i--) {
                    int ent = DAT_80611d74 + tableOff + entOff + 0xee0;
                    if (*(int*)(ent + 0x14) == 3) { carrierState = ent; break; }
                    entOff += 0x24;
                }
            }
            break;
        }
        viewIdx++;
        tableOff += 0xcc;
        found = -1;
    }

    if (-1 < found) {
        int rowBase = found * 0xcc + DAT_80611d74;
        int rows = *(int*)(rowBase + 0xedc);
        for (int i = 0; i < rows; i++) {
            double fade = (double)*(float*)(rowBase + 0xed0);
            FUN_801236f0(fade, fade);
        }
    }

    FUN_8016f00c(1);
    if (carrierState == 0) {
        unsigned char* str = (unsigned char*)get_string_by_id(0x15e4);
        uint shadow = FUN_80132348(dmgAlpha, 0x40);
        uint color = FUN_80132348(dmgAlpha, 0x6060607f);
        double w = text_get_char_width(str);
        text_draw_char_shadowed(
            (double)(float)((double)(float)((double)CONCAT44(0x43300000, rightMag ^ 0x80000000) - DOUBLE_80615738) - w),
            (double)(float)((double)CONCAT44(0x43300000, yMag ^ 0x80000000) - DOUBLE_80615738),
            str, color, shadow);
    } else {
        double phaseD = (double)(CONCAT44(0x43300000, DAT_80611928 % 0x1e) ^ 0x80000000);
        double pulse = (double)(FLOAT_80615734 *
            (&DAT_80583228)[(int)(FLOAT_806157c0 * FLOAT_80615748 * FLOAT_806157c4 *
                ((float)(phaseD - DOUBLE_80615738) / FLOAT_806157cc) + FLOAT_80615734) & 0x7ff] +
            FLOAT_80615734);
        unsigned char* str = (unsigned char*)get_string_by_id(0x15e3);
        uint shadow = FUN_80132348(dmgAlpha, 0x40);
        uint pc = FUN_803825c0((double)(float)((double)FLOAT_80615750 * pulse));
        uint color = FUN_80132348(dmgAlpha,
            (pc & 0xff) << 8 | (pc & 0xff) << 0x10 | 0x7f000000 | 0x7f);
        double w = text_get_char_width(str);
        text_draw_char_shadowed(
            (double)(float)((double)(float)((double)CONCAT44(0x43300000, rightMag ^ 0x80000000) - DOUBLE_80615738) - w),
            (double)(float)((double)CONCAT44(0x43300000, yMag ^ 0x80000000) - DOUBLE_80615738),
            str, color, shadow);
    }
    FUN_80382730();
    return;
}

// ============================================================================
// 0x80123ecc | hud_prompt_slide_tick
// Per-frame slide/expand animation for the active view's secondary prompt state
// (DAT_80579dd0, 0x177c stride). Advances a 4-state machine (open/opening/hold/
// closing) driving a width value, a highlight ramp, and a horizontal offset that
// eases toward its target when the delta exceeds a threshold.
// ============================================================================
extern "C" void hud_prompt_slide_tick(void) {
    if ((-1 < *(int*)(DAT_80611d74 + 4)) &&
        (0 != *(int*)((int)&DAT_80579dd0 + *(int*)(DAT_80611d74 + 4) * 0x177c))) {
        int base = *(int*)(DAT_80611d74 + 4) * 0x177c;
        int*   stateWord = (int*)((int)&DAT_80579dd0 + base);   // +0x00 state machine word
        float* stateW    = (float*)((int)&DAT_80579dd8 + base); // +0x08 width/expand value
        float* stateT    = (float*)((int)&DAT_80579dd4 + base); // +0x04 hold timer
        float* hi        = (float*)((int)&DAT_80579de4 + base); // +0x14 highlight ramp
        float* off       = (float*)((int)&DAT_80579ddc + base); // +0x0c horizontal offset (current)
        float* target    = (float*)((int)&DAT_80579de0 + base); // +0x10 horizontal offset (target)

        float delta = FLOAT_806108ec *
            (FLOAT_80615718 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738));
        if (DAT_80611934 == 8) {
            *stateWord = 3;
        }
        float zero = FLOAT_80615730;
        float one = FLOAT_80615718;
        if (*stateWord == 1) {
            // opening: ramp width up, clamp to 1.0, then advance the hold timer
            *stateW = FLOAT_8061576c * delta + *stateW;
            if (one < *stateW) *stateW = one; else *stateW = *stateW;
            *stateT = *stateT + delta;
            if (FLOAT_80615718 < *stateT) {
                *stateWord = 2;
            }
        } else {
            // closing (state 3 uses the faster rate); ramp width down, clamp to 0
            float rate = one;
            if (*stateWord == 3) rate = FLOAT_8061576c;
            *stateW = -(delta * rate - *stateW);
            if (*stateW < zero) {
                *stateW = zero;
                *stateWord = 0;
            }
        }

        // highlight ramp (decays toward 0)
        if (FLOAT_80615730 <= *hi) {
            if (FLOAT_80615730 < *hi) {
                *hi = -(FLOAT_80615748 * delta - *hi);
                if (*hi < FLOAT_80615730) *hi = FLOAT_80615730;
            }
        } else {
            *hi = FLOAT_80615748 * delta + *hi;
            if (FLOAT_80615730 < *hi) *hi = FLOAT_80615730;
        }

        // horizontal offset easing (current toward target)
        if (FLOAT_80615814 <= ABS(*target - *off)) {
            float rateDelta = FLOAT_80615718 /
                (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738);
            if (*target <= *off) {
                float ease = (rateDelta * (*off - *target)) / FLOAT_80615884;
                if (FLOAT_8061572c < ease) ease = FLOAT_8061572c;
                *off = -(ease * FLOAT_806108e8 - *off);
                if (*off < *target) *off = *target;
            } else {
                float ease = (rateDelta * (*target - *off)) / FLOAT_80615884;
                if (FLOAT_8061572c < ease) ease = FLOAT_8061572c;
                *off = ease * FLOAT_806108e8 + *off;
                if (*target < *off) *off = *target;
            }
        } else {
            *off = *target;
        }
    }
    return;
}
