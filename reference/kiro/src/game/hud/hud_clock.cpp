// Batch 348 | Date: 2026-09-04
// HUD session clock + timer/charge icon widgets.
//
// This module owns a small wall-clock timer used by the HUD:
//   - hud_clock_tick                 (0x8012de40) per-frame h/m/s advance
//   - hud_clock_init_from_time_of_day(0x8012ddc0) seed from platform time-of-day
//   - hud_int_to_bcd_byte            (0x8012dd00) int -> packed BCD byte
//   - hud_clock_write_bcd_buffer     (0x8012dd30) time value -> hh/mm/ss BCD buffer
// plus two related HUD icon renderers that overlay hud_charge_meter_render:
//   - hud_timer_icon_render          (0x8012dba0) fixed sprite 0x119 + charge meter
//   - hud_charge_icon_render         (0x8012d954) sprite-table icon + charge meter
//
// The clock counters live in a fixed globals block at 0x80612000:
//   +0x00 fractional accumulator, +0x04 seconds, +0x08 minutes, +0x0c hours,
//   +0x10..+0x18 a saved copy (set at init). Rollovers at 60/60/24.
//
// Subsystem: game (HUD)
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ---- clock global block (0x80612000) ----
extern float DAT_80612000;   // fractional second accumulator
extern int   DAT_80612004;   // live seconds
extern int   DAT_80612008;   // live minutes
extern int   DAT_8061200c;   // live hours
extern int   DAT_80612010;   // saved seconds
extern int   DAT_80612014;   // saved minutes
extern int   DAT_80612018;   // saved hours

extern float DAT_806108e0;   // frame delta (seconds)
extern float DAT_806109c0;   // ticks-per-second (rollover threshold)
extern int   DAT_800000f8;   // platform timer bus frequency

// ---- time-of-day helpers (raw) ----
// FUN_803a0c30: read raw platform time (64-bit ticks in hi:lo).
// FUN_803a0e50: decompose a 64-bit tick count into out[0]=sec,out[1]=min,out[2]=hour...
extern unsigned long long FUN_803a0c30(void);
extern void FUN_803a0e50(unsigned int hi, unsigned int lo, int* out);

// ---- icon-render externs (raw / named) ----
extern float FLOAT_8061082c;
extern float FLOAT_80610830;
extern float FLOAT_8061571c;
extern float FLOAT_80615780;
extern float FLOAT_806157e8;
extern float FLOAT_80615a18;
extern double DOUBLE_80615738;   // uint->double bias

extern int   DAT_8040d1cc;       // timer icon blend/color mode
extern int   DAT_8040c5b4;       // charge sprite-table: texture id  (0x1c stride)
extern int   DAT_8040c5b8;       // charge sprite-table: radius/scale (0x1c stride)
extern int   DAT_8040c5c0;       // charge sprite-table: blend mode   (0x1c stride)
extern int   DAT_80578680;       // per-player render-index table (0x5df stride)
extern int   DAT_80611748[];     // viewport rect (ints)
extern int   DAT_80611d74;       // active HUD context ptr
extern int   DAT_80610a20;       // sprite descriptor table base ptr (*DAT + id*0x58)

extern int    sprite_get_texture_id(int id);
extern int    hud_is_special_layout(void);
extern void   hud_compute_fade_channel(int ch);
extern void   hud_charge_meter_render(double x, double y);
extern void   FUN_8016f00c(int blend);   // set sprite blend mode
extern void   FUN_8016e77c(int tex);     // bind sprite texture
extern void   FUN_801708b4(double x0, double y0, double x1, double y1, int u0, int v0,
                           unsigned int u1, unsigned int v1); // draw sprite quad

extern unsigned long long FUN_803826e8(void);  // HUD push (timer icon), context in high word
extern void   FUN_80382734(void);               // HUD pop  (timer icon)
extern unsigned long long FUN_803826ec(void);   // HUD push (charge icon)
extern void   FUN_80382738(void);               // HUD pop  (charge icon)

// ----------------------------------------------------------------------------
// hud_int_to_bcd_byte (0x8012dd00)
// Low two decimal digits of n as packed BCD.
// ----------------------------------------------------------------------------
unsigned int hud_int_to_bcd_byte(int param_1)
{
    return (unsigned int)((((param_1 % 100) / 10) * 0x10 + (param_1 % 100) % 10) & 0xff);
}

// ----------------------------------------------------------------------------
// hud_clock_write_bcd_buffer (0x8012dd30)
// Scale a raw timer value into h/m/s and write packed-BCD bytes into buffer.
// ----------------------------------------------------------------------------
void hud_clock_write_bcd_buffer(int param_1, unsigned int param_2)
{
    int local[6];  // out[0..5]: sec,min,hour,... (as produced by FUN_803a0e50)

    unsigned int freq = (unsigned int)DAT_800000f8 >> 2;
    unsigned long long scaled = (unsigned long long)param_2 * (unsigned long long)freq;
    FUN_803a0e50((unsigned int)(scaled >> 0x20), (unsigned int)(param_2 * freq), local);

    *(char*)(param_1 + 1) = (char)hud_int_to_bcd_byte(local[0]);
    *(char*)(param_1 + 2) = (char)hud_int_to_bcd_byte(local[1]);
    *(char*)(param_1 + 3) = (char)hud_int_to_bcd_byte(local[2]);
    *(char*)(param_1 + 5) = (char)hud_int_to_bcd_byte(local[3]);
    *(char*)(param_1 + 6) = (char)hud_int_to_bcd_byte(local[4] + 1);
    *(char*)(param_1 + 7) = (char)hud_int_to_bcd_byte(local[5]);
}

// ----------------------------------------------------------------------------
// hud_clock_init_from_time_of_day (0x8012ddc0)
// Seed the HUD clock counters (live + saved) from the platform time-of-day.
// ----------------------------------------------------------------------------
void hud_clock_init_from_time_of_day(void)
{
    int local[6];
    unsigned long long t = FUN_803a0c30();
    FUN_803a0e50((unsigned int)(t >> 0x20), (unsigned int)t, local);

    DAT_80612000 = 0.0f;
    DAT_8061200c = local[2] % 0x18;
    DAT_80612018 = local[2] % 0x18;
    DAT_80612008 = local[1] % 0x3c;
    DAT_80612014 = local[1] % 0x3c;
    DAT_80612004 = local[0] % 0x3c;
    DAT_80612010 = local[0] % 0x3c;
}

// ----------------------------------------------------------------------------
// hud_clock_tick (0x8012de40)
// Per-frame advance of the HUD clock: accumulate frame delta, roll over
// seconds -> minutes (60) -> hours (24).
// ----------------------------------------------------------------------------
void hud_clock_tick(void)
{
    DAT_80612000 = DAT_80612000 + DAT_806108e0;
    if (DAT_80612000 < DAT_806109c0)
        return;
    DAT_80612000 = DAT_80612000 - DAT_806109c0;
    DAT_80612004 = DAT_80612004 + 1;
    if (DAT_80612004 != 0x3c)
        return;
    DAT_80612004 = 0;
    DAT_80612008 = DAT_80612008 + 1;
    if (DAT_80612008 != 0x3c)
        return;
    DAT_80612008 = 0;
    DAT_8061200c = DAT_8061200c + 1;
    if (DAT_8061200c != 0x18)
        return;
    DAT_8061200c = 0;
}

// ----------------------------------------------------------------------------
// hud_timer_icon_render (0x8012dba0)
// Draw a centered timer icon (sprite 0x119) sized by FLOAT_80615a18, then the
// charge meter. param_3 is the widget height (screen space).
// ----------------------------------------------------------------------------
void hud_timer_icon_render(unsigned int param_1, unsigned int param_2, int param_3)
{
    unsigned long long uVar5 = FUN_803826e8();
    int iVar1 = (int)(uVar5 >> 0x20);
    (void)param_1; (void)param_2;
    if ((*(int*)(iVar1 + 8) != 0) && (-1 <= *(int*)(iVar1 + 4))) {
        int cx = (int)uVar5 >> 1;
        int cy = param_3 >> 1;
        int tex = sprite_get_texture_id(0x119);
        FUN_8016f00c(DAT_8040d1cc);
        FUN_8016e77c(tex);
        int desc = *(int*)DAT_80610a20 + tex * 0x58;
        double fx = (double)cx;
        double fy = (double)cy;
        FUN_801708b4((double)((float)fx - FLOAT_80615a18),
                     (double)((float)fy - FLOAT_80615a18),
                     (double)(FLOAT_80615a18 * FLOAT_8061082c + (float)fx),
                     (double)(FLOAT_80615a18 * FLOAT_80610830 + (float)fy),
                     0, 0,
                     (unsigned int)*(unsigned short*)(desc + 0x4e) << 4,
                     (unsigned int)*(unsigned short*)(desc + 0x52) << 4);
        hud_charge_meter_render(fx, fy);
    }
    FUN_80382734();
}

// ----------------------------------------------------------------------------
// hud_charge_icon_render (0x8012d954)
// Draw a charge/timer icon from the sprite table (DAT_8040c5b4 group) at a
// camera-lerped position, with a special-layout fade path, then the charge
// meter. Gated on an active condition + valid player slot.
// ----------------------------------------------------------------------------
void hud_charge_icon_render(void)
{
    FUN_803826ec();
    int ctx = DAT_80611d74;
    if (((*(int*)(*(int*)(ctx + 0x1398) + 0x74) != 0) || (*(int*)(ctx + 0xdf4) != 0)) &&
        (-1 <= *(int*)(ctx + 4))) {
        int idx = (&DAT_80578680)[*(int*)(ctx + 4) * 0x5df] * 0x1c;

        // camera-lerped X/Y (viewport origin + lerp of the per-view offset)
        double px = (double)(float)(
            (double)(DAT_80611748[0] + DAT_80611748[4]) +
            (double)(int)(*(float*)(ctx + 0xe04) *
                          (float)(FLOAT_8061571c * (float)(-(double)DAT_80611748[4] - DOUBLE_80615738))));
        double py = (double)(float)(
            (double)(DAT_80611748[5] + DAT_80611748[9]) +
            (double)(int)(*(float*)(ctx + 0xe08) *
                          (float)(FLOAT_8061571c * (float)(-(double)DAT_80611748[9] - DOUBLE_80615738))));

        if (*(int*)((int)&DAT_8040c5b4 + idx) != -1) {
            int tex = sprite_get_texture_id(*(int*)((int)&DAT_8040c5b4 + idx));
            int blend = *(int*)((int)&DAT_8040c5c0 + idx);
            double radius = (double)*(float*)((int)&DAT_8040c5b8 + idx);
            if (hud_is_special_layout() != 0) {
                blend = 1;
                hud_compute_fade_channel(0x20);
            }
            FUN_8016f00c(blend);
            FUN_8016e77c(tex);
            int desc = *(int*)DAT_80610a20 + tex * 0x58;
            FUN_801708b4((double)(float)(px - (double)(float)(radius * (double)FLOAT_8061082c)),
                         (double)(float)(py - (double)(float)(radius * (double)FLOAT_80610830)),
                         (double)(float)(px + (double)(float)(radius * (double)FLOAT_8061082c)),
                         (double)(float)(py + (double)(float)(radius * (double)FLOAT_80610830)),
                         0, 0,
                         (unsigned int)*(unsigned short*)(desc + 0x4e) << 4,
                         (unsigned int)*(unsigned short*)(desc + 0x52) << 4);
            hud_charge_meter_render(px, py);
        }
    }
    FUN_80382738();
}
