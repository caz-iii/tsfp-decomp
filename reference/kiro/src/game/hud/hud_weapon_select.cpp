// Batch 343 | Date: 2026-09-04
// HUD weapon-select wheel: the weapon/gadget selection bar shown while the
// player holds the switch button. Driven from hud_render_player_view and the
// weapon-cycle logic (FUN_801b5578 group). Covers:
//
//   0x80124994 hud_weapon_select_count_categories (count distinct displayable categories)
//   0x80124a4c hud_weapon_select_render           (MASSIVE: lay out + draw the wheel)
//   0x8012543c hud_weapon_select_begin_switch     (start the open/switch animation)
//   0x80125484 hud_weapon_select_prev             (reset highlight + begin switch)
//   0x801254cc hud_weapon_select_next             (reset highlight + begin switch)
//   0x80125514 hud_weapon_select_cycle_up         (nudge highlight ramp up, begin switch)
//   0x80125578 hud_weapon_select_cycle_down       (nudge highlight ramp down, begin switch)
//   0x801255dc hud_weapon_select_begin_close      (set anim state 3 = closing)
//   0x80125608 hud_weapon_select_stub             (empty no-op)
//
// The per-view animation state lives in the DAT_80579dd0 block (0x177c stride):
//   +0x00 state word, +0x04 hold timer, +0x08 width, +0x0c offset (cur),
//   +0x10 offset (target), +0x14 highlight ramp.
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
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_8061576c;
extern float  FLOAT_806157b0;
extern float  FLOAT_806157f0;
extern float  FLOAT_80615778;
extern float  FLOAT_80615838;
extern float  FLOAT_8061597c;
extern float  FLOAT_80615974;
extern float  FLOAT_80615978;
extern float  FLOAT_80615980;
extern float  FLOAT_80615984;
extern float  FLOAT_80615988;
extern float  FLOAT_8061598c;
extern float  FLOAT_80615990;
extern float  FLOAT_80615994;
extern float  FLOAT_80615998;
extern float  FLOAT_8061599c;
extern float  FLOAT_806159a0;
extern double DOUBLE_80615738; // uint->double bias

// data / globals
extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611748[];   // viewport rect
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_8057ff4c, DAT_8057ff50, DAT_8057ff54, DAT_8057ff58; // layout insets
extern int    DAT_80611fd8;     // switch-anim direction/mode selector
extern int    DAT_8043e264;     // weapon slot -> category id table (0xf stride, via &NAME)
extern int    DAT_8044098c;     // ordered weapon slot list (0x46 entries, via &NAME)
extern int    DAT_8043d9a0;     // weapon slot -> style id table (5-word stride, via &NAME)
extern float  DAT_8043d9a4;     // weapon slot -> panel width  (0x14 stride, via &NAME)
extern float  DAT_8043d9a8;     // weapon slot -> panel height (0x14 stride, via &NAME)
extern int    DAT_80579dd0;     // per-view anim state word (0x177c stride, via &NAME)
extern float  DAT_80579dd4;     //   +0x04 hold timer
extern float  DAT_80579dd8;     //   +0x08 width
extern float  DAT_80579ddc;     //   +0x0c offset (current)
extern float  DAT_80579de0;     //   +0x10 offset (target)
extern float  DAT_80579de4;     //   +0x14 highlight ramp

// named helpers (raw)
extern void   FUN_803826dc(void);            // HUD push (count categories)
extern void   FUN_80382728(void);            // HUD pop  (count categories)
extern void   FUN_803826c8(void);            // HUD push (wheel render)
extern void   FUN_80382714(void);            // HUD pop  (wheel render)
extern int    hud_weapon_slot_sprite(int slot);        // batch 342
extern double hud_scale_screen_height(double value);   // batch 342
extern void   hud_weapon_panel_draw(double, double, double, double, double, double, double, double,
                                    uint, uint, int, int, uint, int, uint, uint,
                                    float, float, float, float, float);  // batch 342
extern void   FUN_8016e8ac(int blend);
extern void   FUN_8016f00c(int mode);
extern void   FUN_8016e84c(void);
extern void   FUN_8016f0a0(int* a, int* b, int* c, int* d);   // save scissor rect
extern void   FUN_8016f0e4(int x, int y, int w, int h);       // set scissor rect
extern int    sprite_get_texture_id(int spriteId);
extern double screen_effect_get_damage_alpha(void);
extern void   hud_render_screen_border(double x0, double y0, double x1, double y1, double alpha);
extern uint   FUN_801aad8c(int ctx, uint slot, int dir);   // next slot in category
extern uint   FUN_801aac98(int ctx, uint slot, int dir);   // prev slot in category
extern int    FUN_801aa93c(int ctx, uint slot);            // slot ammo/charge fraction (as bits)

// forward decls (this file)
extern "C" {
int  hud_weapon_select_count_categories(void);
void hud_weapon_select_render(void);
void hud_weapon_select_begin_switch(int view);
void hud_weapon_select_prev(void);
void hud_weapon_select_next(void);
void hud_weapon_select_cycle_up(void);
void hud_weapon_select_cycle_down(void);
void hud_weapon_select_begin_close(void);
void hud_weapon_select_stub(void);
}

// ============================================================================
// 0x80124994 | hud_weapon_select_count_categories
// Returns the number of distinct weapon categories the player currently owns
// that have a displayable HUD sprite. Walks the 0x46-entry ordered slot list
// (DAT_8044098c), marks each category (DAT_8043e264) seen with a valid sprite.
// ============================================================================
extern "C" int hud_weapon_select_count_categories(void) {
    int seen[78];
    FUN_803826dc();
    for (int i = 0; i < 78; i++) seen[i] = 0;
    int count = 0;
    int off = 0;
    for (int i = 0; i < 0x46; i++) {
        int slot = *(int*)((int)&DAT_8044098c + off);
        int cat = (&DAT_8043e264)[slot * 0xf];
        if ((seen[cat] == 0) && (hud_weapon_slot_sprite(slot) != -1)) {
            seen[cat] = 1;
            count++;
        }
        off += 4;
    }
    FUN_80382728();
    return count;
}

// ============================================================================
// 0x80124a4c | hud_weapon_select_render  (MASSIVE)
// Draws the weapon-select wheel: counts the owned categories, computes the
// centered layout offset (animated in/out by the switch state DAT_80611fd8 and
// the per-view offset ramp), draws the framed background border, then draws each
// owned weapon's panel (hud_weapon_panel_draw) with its category slot, ammo
// fraction, and selection/animation state.
// ============================================================================
extern "C" void hud_weapon_select_render(void) {
    int savedX, savedY, savedW, savedH;
    uint colX[70];   // per-category column index (auStack_340)
    int  catCount[70]; // per-category owned count (local_228)

    FUN_803826c8();
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        int view = *(int*)(DAT_80611d74 + 4) * 0x177c;
        int ctx = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
        int columns = hud_weapon_select_count_categories();

        // first pass: assign a column index per category and count total columns
        for (int i = 0; i < 70; i++) catCount[i] = 0;
        double rowAdvance = (double)FLOAT_80615730;
        int usedColumns = 0;
        int off = 0;
        for (int i = 0; i < 0x46; i++) {
            int slot = *(int*)((int)&DAT_8044098c + off);
            int cat = (&DAT_8043e264)[slot * 0xf];
            if (hud_weapon_slot_sprite(slot) != -1) {
                if (catCount[cat] == 0) {
                    int sel = *(int*)(ctx + 0x94);
                    colX[cat] = (uint)usedColumns;
                    if (cat == (&DAT_8043e264)[sel * 0xf]) {
                        rowAdvance = (double)(float)(rowAdvance + (double)FLOAT_80615978);
                    }
                    usedColumns++;
                    rowAdvance = (double)(float)(rowAdvance + (double)FLOAT_8061597c);
                }
                catCount[cat] = catCount[cat] + 1;
            }
            off += 4;
        }

        // centered horizontal offset for the whole bar
        double centerBias = -(double)(float)((double)FLOAT_80615734 *
            (double)(FLOAT_80615980 *
                     (float)((double)CONCAT44(0x43300000, columns ^ 0x80000000) - DOUBLE_80615738) +
                     (float)((double)FLOAT_80615730 *
                             (double)(float)((double)CONCAT44(0x43300000, (columns - 1) ^ 0x80000000) - DOUBLE_80615738))) -
            (double)FLOAT_806157b0);

        // animated open/close offset from the switch state
        double slide;
        if (DAT_80611fd8 == 0) {
            slide = (double)FLOAT_8061576c;
        } else if (DAT_80611fd8 == 1) {
            double v = (double)FLOAT_80615730;
            if (((double)FLOAT_80615984 < v) && (v <= (double)FLOAT_80615988)) {
                v = (double)(float)(v - (double)FLOAT_8061598c);
            }
            *(float*)((int)&DAT_80579de0 + view) = (float)v;
            slide = (double)(FLOAT_8061576c - *(float*)((int)&DAT_80579ddc + view));
        } else {
            slide = (double)(float)((double)FLOAT_806157b0 - (double)FLOAT_80615730);
        }

        FUN_8016e8ac(0);
        FUN_8016f00c(1);
        FUN_8016f0a0(&savedX, &savedY, &savedW, &savedH);

        // background border bounds (viewport-relative)
        uint spanY = (DAT_80611748[6] - DAT_8057ff58) - (DAT_8057ff50 + DAT_80611748[5]) ^ 0x80000000;
        uint leftMag = DAT_8057ff4c + *DAT_80611748 ^ 0x80000000;
        double topD = (double)CONCAT44(0x43300000, DAT_8057ff50 + DAT_80611748[5] ^ 0x80000000);
        double borderTop = (double)(FLOAT_80615778 *
            (float)(slide * (double)(float)((double)CONCAT44(0x43300000, spanY) - DOUBLE_80615738)) +
            (float)(topD - DOUBLE_80615738));
        double borderLeft = (double)(float)((double)CONCAT44(0x43300000, leftMag) - DOUBLE_80615738);
        double borderRight = (double)(float)((double)CONCAT44(0x43300000, DAT_80611748[1] - DAT_8057ff54 ^ 0x80000000) - DOUBLE_80615738);
        double rowHeight = hud_scale_screen_height((double)FLOAT_8061597c);
        double borderBottom = (double)(float)((double)(FLOAT_80615778 *
            (float)(slide * (double)(float)((double)CONCAT44(0x43300000, spanY) - DOUBLE_80615738)) +
            (float)(topD - DOUBLE_80615738)) + rowHeight);

        // second pass: expand border to fit each owned weapon's panel
        bool started = false;
        off = 0;
        for (int i = 0; i < 0x46; i++) {
            uint slot = *(uint*)((int)&DAT_8044098c + off);
            int cat = (&DAT_8043e264)[slot * 0xf];
            int spr = hud_weapon_slot_sprite(slot);
            if ((*(unsigned char*)(DAT_80611d74 + cat + 0xcd4) == slot) && (spr != -1)) {
                double panelW = (double)FLOAT_80615990;
                double panelH = (double)FLOAT_80615994;
                double colD = (double)(CONCAT44(0x43300000, colX[cat]) ^ 0x80000000);
                double colCenter = (double)(float)((double)(float)(colD - DOUBLE_80615738) *
                                                   (double)FLOAT_80615980 + centerBias);
                int tex = sprite_get_texture_id(spr);
                if (*(short*)(*DAT_80610a20 + tex * 0x58 + 0x4e) == 0x80) {
                    panelW = (double)FLOAT_80615998;
                    panelH = (double)FLOAT_80615980;
                }
                float pw = (float)(colCenter + (double)(FLOAT_80615734 * (float)(panelH - panelW) +
                                                        FLOAT_80615734 * (float)((double)FLOAT_80615980 - panelH)));
                uint spanX = (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748) ^ 0x80000000;
                if (started) {
                    double lD = (double)CONCAT44(0x43300000, DAT_8057ff4c + *DAT_80611748 ^ 0x80000000);
                    borderRight = (double)(FLOAT_80615778 *
                        (float)(panelW * (double)(float)((double)CONCAT44(0x43300000, spanX) - DOUBLE_80615738)) +
                        FLOAT_80615778 * pw * (float)((double)CONCAT44(0x43300000, spanX) - DOUBLE_80615738) +
                        (float)(lD - DOUBLE_80615738));
                } else {
                    started = true;
                    double lD = (double)CONCAT44(0x43300000, DAT_8057ff4c + *DAT_80611748 ^ 0x80000000);
                    borderLeft = (double)(float)((double)FLOAT_80615778 *
                        (double)(pw * (float)((double)CONCAT44(0x43300000, spanX) - DOUBLE_80615738)) +
                        (double)(float)(lD - DOUBLE_80615738));
                    borderRight = (double)(float)((double)FLOAT_80615778 *
                        (double)(float)(panelW * (double)(float)((double)CONCAT44(0x43300000, spanX) - DOUBLE_80615738)) +
                        borderLeft);
                }
            }
            off += 4;
        }

        // pad and clamp the border to the viewport, then draw it
        uint padMag = DAT_8057ff4c + *DAT_80611748 ^ 0x80000000;
        double spanXd = (double)(CONCAT44(0x43300000, (DAT_80611748[1] - DAT_8057ff54) - (DAT_8057ff4c + *DAT_80611748)) ^ 0x80000000);
        double pad = (double)(FLOAT_80615778 * FLOAT_8061576c * (float)(spanXd - DOUBLE_80615738));
        borderLeft = (double)(float)(borderLeft - pad);
        double leftClamp = (double)(float)((double)CONCAT44(0x43300000, padMag) - DOUBLE_80615738);
        if (borderLeft < leftClamp) borderLeft = leftClamp;
        uint rightMag = DAT_80611748[1] - DAT_8057ff54 ^ 0x80000000;
        borderRight = (double)(float)(borderRight + pad);
        double rightClamp = (double)(float)((double)CONCAT44(0x43300000, rightMag) - DOUBLE_80615738);
        if (rightClamp < borderRight) borderRight = rightClamp;

        if ((FLOAT_8061576c < (float)(borderRight - borderLeft)) &&
            (FLOAT_8061576c < (float)(borderBottom - borderTop))) {
            double dmg = screen_effect_get_damage_alpha();
            hud_render_screen_border(borderLeft, borderTop, borderRight, borderBottom,
                                     (double)(float)((double)FLOAT_80615718 - dmg));
            FUN_8016e84c();
            FUN_8016f0e4((int)borderLeft + 1, (int)borderTop + 1, (int)borderRight - 1, (int)borderBottom - 1);
        }

        // third pass: draw each owned weapon panel
        off = 0;
        for (int i = 0; i < 0x46; i++) {
            uint slot = *(uint*)((int)&DAT_8044098c + off);
            int cat = (&DAT_8043e264)[slot * 0xf];
            int spr = hud_weapon_slot_sprite(slot);
            if (spr != -1) {
                bool wrapHighlight = false;
                double colD = (double)(CONCAT44(0x43300000, colX[cat] ^ 0x80000000) ^ 0);
                double colCenter = (double)(float)((double)(float)((double)(CONCAT44(0x43300000, colX[cat]) ^ 0x80000000) - DOUBLE_80615738) *
                                                   (double)FLOAT_80615980 + centerBias);
                (void)colD;
                uint prevSlot = slot;
                uint nextSlot = slot;
                double slideCur = slide;
                if (1 < catCount[cat]) {
                    nextSlot = FUN_801aad8c(ctx, slot, 1);
                    prevSlot = FUN_801aac98(ctx, slot, 1);
                    uint sel = *(uint*)(ctx + 0x94);
                    if (((&DAT_8043e264)[sel * 0xf] == (&DAT_8043e264)[slot * 0xf]) &&
                        (((*(float*)((int)&DAT_80579de4 + view) < FLOAT_80615730) && (nextSlot == sel)) ||
                         ((FLOAT_80615730 < *(float*)((int)&DAT_80579de4 + view)) && (prevSlot == sel)))) {
                        wrapHighlight = true;
                    }
                }

                if ((*(unsigned char*)(DAT_80611d74 + cat + 0xcd4) == slot) || wrapHighlight) {
                    float ramp = *(float*)((int)&DAT_80579de4 + view);
                    double panelW = (double)FLOAT_80615990;
                    double panelH = (double)FLOAT_80615994;
                    double ramp3 = (double)(ramp * ramp * ramp);
                    double width = (double)*(float*)((int)&DAT_80579dd8 + view);
                    int style = (&DAT_8043d9a0)[slot * 5];
                    double bob = (double)FLOAT_80615730;
                    double hold = (double)FLOAT_80615730;
                    int tex = sprite_get_texture_id(spr);
                    if (*(short*)(*DAT_80610a20 + tex * 0x58 + 0x4e) == 0x80) {
                        panelW = (double)FLOAT_80615998;
                        panelH = (double)FLOAT_80615980;
                    }
                    double panelX = (double)(float)((double)FLOAT_80615734 *
                        (double)(float)((double)FLOAT_80615980 - panelH) + colCenter);
                    uint sel = *(uint*)(ctx + 0x94);
                    if (sel == slot) {
                        slideCur = (double)(float)(ramp3 * (double)FLOAT_8061599c + slideCur);
                    } else if (wrapHighlight) {
                        double adj;
                        if ((double)FLOAT_80615730 <= ramp3) adj = ramp3 - (double)FLOAT_80615718;
                        else adj = (double)FLOAT_80615718 + ramp3;
                        slideCur = (double)(float)((double)(float)adj * (double)FLOAT_80615980 + slideCur);
                        width = (double)(float)(width * ABS(ramp3));
                    }
                    if ((sel == slot) && (*(int*)((int)&DAT_80579dd0 + view) == 1)) {
                        bob = (double)(FLOAT_80615748 * (FLOAT_80615734 - *(float*)((int)&DAT_80579dd4 + view)));
                        if (bob < (double)FLOAT_80615730) bob = (double)FLOAT_80615730;
                        hold = (double)(FLOAT_8061576c * *(float*)((int)&DAT_80579dd4 + view));
                        if ((double)FLOAT_80615718 < hold) hold = (double)FLOAT_80615718;
                    }
                    int ammoBits = FUN_801aa93c(ctx, slot);
                    uint isSel = countLeadingZeros(slot - *(int*)(ctx + 0x94));
                    hud_weapon_panel_draw(
                        panelX, slideCur, panelW, (double)FLOAT_8061597c,
                        (double)*(float*)((int)&DAT_8043d9a4 + slot * 0x14),
                        (double)*(float*)((int)&DAT_8043d9a8 + slot * 0x14),
                        width, bob, isSel >> 5, (uint)ammoBits, spr, style,
                        (uint)(slot != prevSlot), (uint)(slot != nextSlot), isSel, 0,
                        (float)hold, FLOAT_80615718, FLOAT_8061576c, FLOAT_806157f0, FLOAT_806159a0);
                }
            }
            off += 4;
        }
        FUN_8016f0e4(savedX, savedY, savedW, savedH);
    }
    FUN_80382714();
    return;
}

// ============================================================================
// 0x8012543c | hud_weapon_select_begin_switch
// Starts the weapon-select open/switch animation for the given view: if the
// anim is idle (state 0) it resets the offset ramp, then sets state 1 (opening)
// and clears the hold timer.
// ============================================================================
extern "C" void hud_weapon_select_begin_switch(int view) {
    if (*(int*)(view + 4) < 0) return;
    int off = *(int*)(view + 4) * 0x177c;
    if (*(int*)((int)&DAT_80579dd0 + off) == 0) {
        *(float*)((int)&DAT_80579ddc + off) = FLOAT_80615730;
        *(float*)((int)&DAT_80579de0 + off) = FLOAT_80615730;
    }
    *(int*)((int)&DAT_80579dd0 + off) = 1;
    *(float*)((int)&DAT_80579dd4 + off) = FLOAT_80615730;
    return;
}

// ============================================================================
// 0x80125484 | hud_weapon_select_prev
// Reset the active view's highlight ramp to 0, then (re)start the switch anim.
// ============================================================================
extern "C" void hud_weapon_select_prev(void) {
    int view = DAT_80611d74;
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        *(float*)((int)&DAT_80579de4 + *(int*)(DAT_80611d74 + 4) * 0x177c) = FLOAT_80615730;
    }
    hud_weapon_select_begin_switch(view);
    return;
}

// ============================================================================
// 0x801254cc | hud_weapon_select_next
// Reset the active view's highlight ramp to 0, then (re)start the switch anim.
// (Identical body to hud_weapon_select_prev; distinct call site for next/prev.)
// ============================================================================
extern "C" void hud_weapon_select_next(void) {
    int view = DAT_80611d74;
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        *(float*)((int)&DAT_80579de4 + *(int*)(DAT_80611d74 + 4) * 0x177c) = FLOAT_80615730;
    }
    hud_weapon_select_begin_switch(view);
    return;
}

// ============================================================================
// 0x80125514 | hud_weapon_select_cycle_up
// Nudge the highlight ramp up by 1.0 (clamped to 1.0) and (re)start the switch
// anim — used when cycling to the next weapon within a category.
// ============================================================================
extern "C" void hud_weapon_select_cycle_up(void) {
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        int off = *(int*)(DAT_80611d74 + 4) * 0x177c;
        float v = FLOAT_80615718 + *(float*)((int)&DAT_80579de4 + off);
        if (FLOAT_80615718 < v) v = FLOAT_80615718;
        *(float*)((int)&DAT_80579de4 + off) = v;
    }
    hud_weapon_select_begin_switch(DAT_80611d74);
    return;
}

// ============================================================================
// 0x80125578 | hud_weapon_select_cycle_down
// Nudge the highlight ramp down (toward the -1.0 floor FLOAT_80615838) and
// (re)start the switch anim — used when cycling to the previous weapon.
// ============================================================================
extern "C" void hud_weapon_select_cycle_down(void) {
    if (-1 < *(int*)(DAT_80611d74 + 4)) {
        int off = *(int*)(DAT_80611d74 + 4) * 0x177c;
        float v = FLOAT_80615838 + *(float*)((int)&DAT_80579de4 + off);
        if (v < FLOAT_80615838) v = FLOAT_80615838;
        *(float*)((int)&DAT_80579de4 + off) = v;
    }
    hud_weapon_select_begin_switch(DAT_80611d74);
    return;
}

// ============================================================================
// 0x801255dc | hud_weapon_select_begin_close
// Set the active view's switch anim to state 3 (closing).
// ============================================================================
extern "C" void hud_weapon_select_begin_close(void) {
    if (*(int*)(DAT_80611d74 + 4) < 0) return;
    *(int*)((int)&DAT_80579dd0 + *(int*)(DAT_80611d74 + 4) * 0x177c) = 3;
    return;
}

// ============================================================================
// 0x80125608 | hud_weapon_select_stub
// Empty no-op (retained call-site placeholder in the weapon-select group).
// ============================================================================
extern "C" void hud_weapon_select_stub(void) {
    return;
}
