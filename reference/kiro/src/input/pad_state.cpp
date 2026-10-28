// pad_state.cpp
// Per-controller input-state build + analog-stick conditioning, sitting on top
// of the batch_492 rumble layer (pad_rumble.cpp). Each physical GameCube pad is
// polled into the 0x30-byte pad-state block at 0x805aca08 (4 channels x 0xc
// stride) by FUN_803a8a14 (PADRead), then pad_build_state distils each channel
// into the engine's logical button/stick record.
//
// Pad-state block layout (per channel, stride 0xc, base DAT_805aca08):
//   +0x00 (u16) raw button bitfield        (DAT_805aca08)
//   +0x02 (u8)  main stick X               (DAT_805aca0a)
//   +0x03 (u8)  main stick Y               (DAT_805aca0b)
//   +0x04 (u8)  c-stick   X                (DAT_805aca0c)
//   +0x05 (u8)  c-stick   Y                (DAT_805aca0d)
//   +0x06 (u8)  L analog trigger           (DAT_805aca0e)
//   +0x07 (u8)  R analog trigger           (DAT_805aca0f)
//   +0x0a (s8)  connection status (-1 = disconnected)   (DAT_805aca12)
//
// Logical state record (out, per channel, base word offsets used by callers):
//   +0x04 connected flag        +0x08 buttons held (prev)
//   +0x0c buttons current       +0x10 pressed (this frame)
//   +0x14 released (this frame)
//   +0x1c/+0x20 main stick X/Y  +0x24/+0x28 c-stick X/Y
//   +0x2c/+0x30 L/R analog      +0x34 rumble target
//
// Batch: batch_493
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Low-level pad services.
extern void FUN_803a8a14(unsigned short *padBlock);  // PADRead into the pad-state block
extern void FUN_803a86a0(unsigned int resetMask);    // PADReset / clear detach latch
extern int  FUN_802e0a9c(void);                       // input-enabled gate predicate

// Named input helpers (defined elsewhere / not yet decompiled).
extern void input_state_clear(int outRecord);         // zero a logical state record
extern int  input_get_controller_id(int logicalIndex);
extern void rumble_set_target(int channel, int level);
extern void rumble_update(void);
extern void rumble_shutdown(void);

// Math helpers.
extern double math_atan2(double y, double x);
extern double math_poly_trig(double angle);
extern double math_sqrt(double v);

// Compiler-runtime spill helpers.
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);

// Frame-guard pairs (in-frame accessor returns a packed r3:r4 pair; pop closes).
extern long long FUN_803826f0(void);
extern void      FUN_8038273c(void);
extern void      FUN_803826e8(void);
extern void      FUN_80382734(void);

// ---- Data ----
extern unsigned short DAT_805aca08;  // pad-state block base (per-channel stride 0xc)
extern int   DAT_803ccfe8;           // button-remap table: [mask, outBits] pairs, stride 8
extern int   DAT_803ccfec;           // (paired outBits field, +4 from mask)

extern int   DAT_80610aa0;           // global input-suppress latch (-1 = normal)
extern unsigned int DAT_806116b8;    // controller-removed flags (u16 used)
extern int   DAT_80610a40;           // c-stick X range divisor
extern int   DAT_80610a44;           // c-stick Y range divisor
extern float FLOAT_80610a48;         // c-stick Y axis scale
extern int   DAT_80612308;           // stick radial-correction enable flag
extern int   DAT_80612610;           // active local player count
extern int   DAT_80612614;           // game-mode id
extern int   DAT_80611014;           // alt (story) player count
extern unsigned int DAT_80612498;    // previous connected mask
extern unsigned int DAT_80611698;    // newly-removed connect mask (working)

extern float  FLOAT_80616828;        // stick gate reference magnitude
extern double DOUBLE_80616830;       // int->double magic bias (0x43300000 form)
extern float  FLOAT_80616838;        // gate-segment angle offset (45deg)
extern float  FLOAT_8061683c;        // stick radial dead-zone (squared)
extern float  FLOAT_8061230c;        // radial-correction gain
}

// ---------------------------------------------------------------------------
// Address: 0x8018c3cc | Name: pad_stick_normalize_dz15 | Date: 2026-09-06
// Apply a +/-15 dead zone to a stick axis pair, rescale by (<<7 / range), clamp
// to [-0x80, 0x7f], and bias to unsigned [0, 0xff] into *outX / *outY.
// ---------------------------------------------------------------------------
void pad_stick_normalize_dz15(int x, int y, int range, int *outX, int *outY)
{
    if (x < 0xf && -0xf < x) {
        x = 0;
    } else if (x < 1) {
        if (x < 0) {
            x = x + 0xf;
        }
    } else {
        x = x - 0xf;
    }
    if (y < 0xf && -0xf < y) {
        y = 0;
    } else if (y < 1) {
        if (y < 0) {
            y = y + 0xf;
        }
    } else {
        y = y - 0xf;
    }
    int sx = (x << 7) / range;
    int sy = (y << 7) / range;
    if (sx < -0x80) sx = -0x80;
    if (0x7f < sx)  sx = 0x7f;
    if (sy < -0x80) sy = -0x80;
    if (0x7f < sy)  sy = 0x7f;
    *outX = sx + 0x80;
    *outY = sy + 0x80;
}

// ---------------------------------------------------------------------------
// Address: 0x8018c480 | Name: pad_stick_gate_correct | Date: 2026-09-06
// Octagonal-gate correction of a raw stick pair: compute the stick angle
// (atan2), evaluate the trig-poly magnitude of the angle and of the angle minus
// the 45-degree segment offset, take the larger, and rescale the axes so the
// gate reaches the reference magnitude. Writes the corrected pair through the
// spill frame's out-pointer pair (r3 hi / r4 lo).
// ---------------------------------------------------------------------------
void pad_stick_gate_correct(unsigned int rawX, unsigned int rawY)
{
    unsigned long long frame = __save_gpr_28();
    float fx = (float)((double)CONCAT44(0x43300000, rawX ^ 0x80000000) - DOUBLE_80616830);
    float fy = (float)((double)CONCAT44(0x43300000, rawY ^ 0x80000000) - DOUBLE_80616830);

    double ang = (double)(float)math_atan2((double)fy, (double)fx);
    double segMag = ABS(math_poly_trig((double)(float)(ang - (double)FLOAT_80616838)));
    double axisMag = ABS(math_poly_trig(ang));
    double mag = axisMag;
    if (axisMag < segMag) {
        mag = segMag;
    }

    float scale = (float)((double)FLOAT_80616828 / mag);
    int *pX = (int *)((unsigned long long)frame >> 0x20);
    int *pY = (int *)frame;
    *pX = (int)(fx * scale);
    *pY = (int)(fy * scale);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018c59c | Name: pad_stick_radial_correct | Date: 2026-09-06
// Radial dead-zone + magnitude correction of a stick pair. If the squared
// magnitude is inside the dead zone, pass the raw axes through unchanged;
// otherwise rescale by a gain applied to the |x|+|y| L1 magnitude relative to
// the reference. Writes the pair through the spill frame's out-pointer pair.
// ---------------------------------------------------------------------------
void pad_stick_radial_correct(unsigned int rawX, unsigned int rawY)
{
    unsigned long long frame = __save_gpr_28();
    unsigned int *pX = (unsigned int *)((unsigned long long)frame >> 0x20);
    unsigned int *pY = (unsigned int *)frame;

    float fx = (float)((double)CONCAT44(0x43300000, rawX ^ 0x80000000) - DOUBLE_80616830);
    float fy = (float)((double)CONCAT44(0x43300000, rawY ^ 0x80000000) - DOUBLE_80616830);
    double ax = ABS((double)fy);
    double ay = ABS((double)fx);
    double magSq = (double)(float)(ay * ay + (double)(float)(ax * ax));

    if (magSq <= (double)FLOAT_8061683c) {
        *pX = rawX;
        *pY = rawY;
    } else {
        double mag = math_sqrt(magSq);
        float scale = FLOAT_8061230c *
                          ABS(((float)(ay / mag) + (float)(ax / mag)) - FLOAT_80616828) +
                      FLOAT_80616828;
        *pX = (int)(fx * scale);
        *pY = (int)(fy * scale);
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018c6cc | Name: pad_cstick_process | Date: 2026-09-06
// Condition the c-stick pair (arrives packed in the spill frame r3:r4): apply a
// +/-15 dead zone, Y-axis scale (FLOAT_80610a48), clamp to +/-range, optional
// radial correction when the gate flag DAT_80612308 is set, then rescale by
// (<<7 / range) with the [-0x80, 0x7f] clamp and unsigned bias into *outX/*outY.
// ---------------------------------------------------------------------------
void pad_cstick_process(int a1, int a2, int range, unsigned int rangeClamp, int *outX, int *outY)
{
    (void)a1;
    (void)a2;
    long long packed = FUN_803826f0();
    int x = (int)((unsigned long long)packed >> 0x20);
    int y = (int)packed;

    if (x < 0xf && -0xf < x) {
        x = 0;
    } else if (x < 1) {
        if (x < 0) x = x + 0xf;
    } else {
        x = x - 0xf;
    }
    if (y < 0xf && -0xf < y) {
        y = 0;
    } else if (y < 1) {
        if (y < 0) y = y + 0xf;
    } else {
        y = y - 0xf;
    }

    unsigned int negClamp = -rangeClamp;
    y = (int)((float)((double)CONCAT44(0x43300000, (unsigned int)y ^ 0x80000000) - DOUBLE_80616830) *
              FLOAT_80610a48);

    if ((int)x < (int)negClamp) x = negClamp;
    if ((int)rangeClamp < (int)x) x = rangeClamp;
    if ((int)y < (int)negClamp) y = negClamp;
    if ((int)rangeClamp < (int)y) y = rangeClamp;

    if (DAT_80612308 != 0) {
        pad_stick_radial_correct((unsigned int)x, (unsigned int)y);
        // pad_stick_radial_correct writes back through the caller-provided pair;
        // the ROM passes &x/&y in the frame, so x/y hold the corrected values.
    }

    int sx = (x << 7) / range;
    int sy = (y << 7) / range;
    if (sx < -0x80) sx = -0x80;
    if (0x7f < sx)  sx = 0x7f;
    if (sy < -0x80) sy = -0x80;
    if (0x7f < sy)  sy = 0x7f;
    *outX = sx + 0x80;
    *outY = sy + 0x80;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018c8b8 | Name: pad_build_state | Date: 2026-09-06
// Build one controller's logical state record from its polled pad-state block.
// (channel arrives in the spill frame's low word, out record in the high word.)
// If the pad is connected and input is not suppressed: shift current buttons to
// held, decode the raw button bitfield through the remap table DAT_803ccfe8,
// fold analog triggers over 0x20 into digital L/R bits (0x200/0x400), compute
// pressed/released edges, condition both sticks, and set the rumble target.
// Otherwise clear the record (and, when suppressed, silence rumble).
// ---------------------------------------------------------------------------
void pad_build_state(void)
{
    unsigned long long frame = __save_gpr_28();
    int outRec = (int)((unsigned long long)frame >> 0x20);
    int channel = (int)frame;
    int base = channel * 0xc;

    // The status byte lives at +0x0a within the channel block; a value of -1
    // (all bits set) means the pad is disconnected, 0 means connected/ok.
    signed char status = ((signed char *)&DAT_805aca08)[base + 0x0a];
    if (status == '\0') {
        if (DAT_80610aa0 == -1 && DAT_806116b8 == 0) {
            *(unsigned int *)(outRec + 8) = *(unsigned int *)(outRec + 0xc);
            *(unsigned int *)(outRec + 0xc) = 0;
            *(int *)(outRec + 4) = 1;

            unsigned char rTrig = ((unsigned char *)&DAT_805aca08)[base + 0x07];
            unsigned char lTrig = ((unsigned char *)&DAT_805aca08)[base + 0x06];
            if (0x20 < rTrig) {
                *(unsigned int *)(outRec + 0xc) |= 0x400;
            }
            if (0x20 < lTrig) {
                *(unsigned int *)(outRec + 0xc) |= 0x200;
            }

            unsigned short raw = (&DAT_805aca08)[base];
            if (raw != 0) {
                int *tbl = &DAT_803ccfe8;
                for (int i = 0; i < 12; i++) {
                    if (((unsigned int)raw & (unsigned int)tbl[i * 2]) != 0) {
                        *(unsigned int *)(outRec + 0xc) |= (unsigned int)tbl[i * 2 + 1];
                    }
                }
            }

            *(unsigned int *)(outRec + 0x2c) = (unsigned int)(lTrig >> 1) + 0x80;
            *(unsigned int *)(outRec + 0x30) = (unsigned int)(rTrig >> 1) + 0x80;

            // The gate-correct routine returns the conditioned pair through its
            // spill frame's out-pointer pair; the ROM stages it in these locals
            // before the final dead-zone/normalize pass.
            int gateX = 0, gateY = 0;
            pad_stick_gate_correct((unsigned int)((signed char *)&DAT_805aca08)[base + 0x02],
                                   (unsigned int)((signed char *)&DAT_805aca08)[base + 0x03]);
            gateX = *(int *)(outRec + 0x1c);
            gateY = *(int *)(outRec + 0x20);
            signed char cx = ((signed char *)&DAT_805aca08)[base + 0x04];
            signed char cy = ((signed char *)&DAT_805aca08)[base + 0x05];
            pad_stick_normalize_dz15(gateX, gateY, 0x38, (int *)(outRec + 0x1c),
                                     (int *)(outRec + 0x20));
            pad_cstick_process((int)cx, (int)cy, DAT_80610a40, (unsigned int)DAT_80610a44,
                               (int *)(outRec + 0x24), (int *)(outRec + 0x28));

            *(unsigned int *)(outRec + 0x10) =
                *(unsigned int *)(outRec + 0xc) & ~*(unsigned int *)(outRec + 8);
            *(unsigned int *)(outRec + 0x14) =
                *(unsigned int *)(outRec + 8) & ~*(unsigned int *)(outRec + 0xc);

            rumble_set_target(channel, *(int *)(outRec + 0x34));
        } else {
            input_state_clear(outRec);
            rumble_set_target(channel, 0);
        }
    } else {
        input_state_clear(outRec);
        *(int *)(outRec + 4) = 0;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Address: 0x8018ca88 | Name: pad_poll_and_update | Date: 2026-09-06
// The low-level per-frame pad poll worker (called by input_update_all). Reads
// all four pads into the pad-state block, builds the disconnect mask and hands
// it to FUN_803a86a0, services rumble, computes the active-controller connect
// mask over the current player set, detects newly-removed controllers, and hard
// -stops rumble on removal.
// ---------------------------------------------------------------------------
void pad_poll_and_update(void)
{
    FUN_803826e8();
    unsigned int connectedMask = 0;
    unsigned int activeMask = 0;

    FUN_803a8a14(&DAT_805aca08);

    unsigned int detachMask = 0;
    int bit = 0;
    int base = 0;
    for (int i = 4; i != 0; i--) {
        if (((signed char *)&DAT_805aca08)[base + 0x0a] == -1) {
            detachMask |= 0x80000000u >> bit;
        } else {
            connectedMask |= 1u << bit;
            DAT_806116b8 &= ~(unsigned int)(1 << bit);
        }
        bit++;
        base += 0xc;
    }
    FUN_803a86a0(detachMask);
    rumble_update();

    int count = DAT_80612610;
    if (100 < DAT_80612614) {
        count = DAT_80611014;
    }
    for (int p = 0; p < count; p++) {
        int cid = input_get_controller_id(p);
        activeMask |= 1u << cid;
    }

    DAT_80611698 = ~(connectedMask | ~DAT_80612498);
    DAT_806116b8 = (DAT_806116b8 | (unsigned short)DAT_80611698) & (unsigned short)activeMask;
    DAT_80612498 = connectedMask;

    if (FUN_802e0a9c() == 0) {
        DAT_806116b8 = 0;
    }
    if (DAT_806116b8 != 0) {
        rumble_shutdown();
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// Address: 0x8018cbc0 | Name: input_get_removed_mask | Date: 2026-09-06
// Return the controller-removed flag mask (u16).
// ---------------------------------------------------------------------------
unsigned short input_get_removed_mask(void)
{
    return (unsigned short)DAT_806116b8;
}

// ---------------------------------------------------------------------------
// Address: 0x8018cbc8 | Name: input_nop_a | Date: 2026-09-06
// Empty hook.
// ---------------------------------------------------------------------------
void input_nop_a(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x8018cbcc | Name: input_nop_b | Date: 2026-09-06
// Empty hook.
// ---------------------------------------------------------------------------
void input_nop_b(void)
{
}
