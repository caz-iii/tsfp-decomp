// Batch 344 | Date: 2026-09-04
// HUD radar blip render. Draws the circular radar/scanner overlay (sprite 0x11d
// dish + 0xfb sweep wedge + 0xfe center dot) and, for each tracked "A11A"-tagged
// entity within range, a blip dot (sprite 0x11d frame) placed by bearing/range
// relative to the player, pulsing when near the sweep angle. Called from the
// radar-panel renderer (FUN_801298b0).
//
//   0x8012910c hud_radar_blips_render
//
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signature + faithful body; raw symbols declared
// per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_80615754;
extern float  FLOAT_806157c0;
extern float  FLOAT_80615848;
extern float  FLOAT_80615860;
extern float  FLOAT_80615884;
extern float  FLOAT_80615814;
extern float  FLOAT_806158dc;
extern float  FLOAT_806159e0;
extern float  FLOAT_806159e4;
extern float  FLOAT_806159e8;
extern float  FLOAT_806157c4;
extern float  FLOAT_80612818;
extern double DOUBLE_80615738;  // uint->double bias
extern double DOUBLE_80615740;  // int->double bias
extern double DOUBLE_806159d8;  // atan2 reference angle

extern int    DAT_80611d54;     // active render/camera context
extern int*   DAT_80610a20;     // texture descriptor table base
extern int    DAT_806109c0;     // frame delta accumulator (uint magnitude)
extern int    DAT_806109c4;     // render width
extern int    DAT_806109c8;     // render height
extern int    DAT_8060f950;     // radar range-x reference
extern int    DAT_8060f954;     // radar range-y reference
extern float  DAT_80583228;     // sine LUT (indexed via &NAME)
extern unsigned char DAT_80000010;   // (address sentinel; unused as data)

// named helpers (raw)
extern unsigned long long FUN_803826d8(void);   // HUD push (radar), returns (cx<<32|cy)
extern void   FUN_80382724(void);                // HUD pop  (radar)
extern int    sprite_get_texture_id(int spriteId);
extern void   FUN_8016e77c(int texId);
extern void   FUN_8016f00c(int mode);
extern void   FUN_80170410(int x0, int y0, int x1, int y1, int u, int v, uint tw, uint th);
extern int    FUN_8023c59c(int tag, int outArr, int max);   // gather tagged entities
extern double FUN_80130d3c(void);                // radar sweep phase
extern double FUN_80390ae4(double a, double ref); // atan2 variant
extern double FUN_80390aa4(double y, double x);   // atan2
extern double FUN_80131538(double x);             // sqrt

// forward decl (this file)
extern "C" void hud_radar_blips_render(uint p1, uint p2, uint boxW, uint boxH, uint color);

// ============================================================================
// 0x8012910c | hud_radar_blips_render
// Draws the radar dish, the rotating sweep wedge (angle from FUN_80130d3c), and
// the center dot at the box center. Then gathers up to 8 'A11A'-tagged entities;
// for each in range it computes the screen offset from bearing+range (sine LUT)
// and draws a blip that flares (FLOAT_806159e4/e8) when close to the sweep angle.
// ============================================================================
extern "C" void hud_radar_blips_render(uint p1, uint p2, uint boxW, uint boxH, uint color) {
    int ents[8];   // local_e0

    unsigned long long push = FUN_803826d8();
    int cx0 = (int)(push >> 0x20);   // box left  (iVar8)
    int cy0 = (int)push;             // box top   (iVar9)
    (void)p1; (void)p2;
    uint cx = ((int)(cx0 + boxW) >> 1) + (uint)((int)(cx0 + boxW) < 0 && ((cx0 + boxW) & 1) != 0);
    uint cy = ((int)(cy0 + boxH) >> 1) + (uint)((int)(cy0 + boxH) < 0 && ((cy0 + boxH) & 1) != 0);

    int n = FUN_8023c59c(0x31313141, (int)(intptr_t)ents, 8);   // 'A11A'
    double sweep = FUN_80130d3c();
    sweep = (double)FUN_80390ae4((double)((FLOAT_80615718 /
        (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80615738)) *
        (float)((double)FLOAT_80615754 * sweep)), DOUBLE_806159d8);
    uint sweepPi = (uint)(FLOAT_806157c0 * (float)sweep + FLOAT_80615734);
    double sweepSin = (double)((float)(&DAT_80583228)[sweepPi & 0x7ff] * FLOAT_80615734);

    FUN_8016f00c(2);
    // radar dish
    int dish = sprite_get_texture_id(0x11d);
    FUN_8016e77c(dish);
    int ddesc = *DAT_80610a20 + dish * 0x58;
    FUN_80170410(cx0, cy0, boxW, boxH, 0, 0,
                 (uint)*(unsigned short*)(ddesc + 0x4e) << 4, (uint)*(unsigned short*)(ddesc + 0x52) << 4);

    // rotating sweep wedge
    int sweepTex = sprite_get_texture_id(0xfb);
    FUN_8016e77c(sweepTex);
    int sdesc = *DAT_80610a20 + sweepTex * 0x58;
    float halfW = (float)(sweepSin * (double)(float)((double)CONCAT44(0x43300000, (boxW - cx0) ^ 0x80000000) - DOUBLE_80615738));
    float halfH = (float)(sweepSin * (double)(float)((double)CONCAT44(0x43300000, (boxH - cy0) ^ 0x80000000) - DOUBLE_80615738));
    int sx0 = (int)((float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738) + halfH);
    int sy0 = (int)((float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738) + halfW);
    int sx1 = (int)((float)((double)CONCAT44(0x43300000, cx ^ 0x80000000) - DOUBLE_80615738) - halfH);
    int sy1 = (int)((float)((double)CONCAT44(0x43300000, cy ^ 0x80000000) - DOUBLE_80615738) - halfW);
    (void)color;
    FUN_80170410(sx0, sy0, sx1, sy1, 0, 0,
                 (uint)*(unsigned short*)(sdesc + 0x4e) << 4, (uint)*(unsigned short*)(sdesc + 0x52) << 4);

    // center dot
    int dot = sprite_get_texture_id(0xfe);
    FUN_8016e77c(dot);
    FUN_80170410(cx - 2, cy - 2, cx + 2, cy + 2, 0, 0, 0x50, 0x50);

    // per-entity blips
    int off = 0;
    for (int i = 0; i < n; i++) {
        int ent = *(int*)((int)(intptr_t)ents + off);
        float dx = *(float*)(DAT_80611d54 + 0x27c) - *(float*)(ent + 100);
        float dz = *(float*)(DAT_80611d54 + 0x284) - *(float*)(ent + 0x6c);
        double rangeSq = (double)((dx * dx + dz * dz) / FLOAT_806159e0);
        if ((*(short*)(ent + 0x94) == (short)0x557) && (rangeSq <= (double)FLOAT_80615814)) {
            double r = FUN_80131538(rangeSq);
            double blip = sweepSin;
            if (ABS((double)(float)(r - blip)) < (double)FLOAT_80615884) {
                uint pi = (uint)(FLOAT_806157c0 * FLOAT_806159e8 *
                    (float)((double)(float)((double)FLOAT_80615884 + r) - blip) + FLOAT_80615734);
                r = (double)(int)(FLOAT_806159e4 * (&DAT_80583228)[pi & 0x7ff] + (float)(4.50360177485416e+15 - DOUBLE_80615738));
            }
            double bearing = (double)FUN_80390aa4((double)(*(float*)(ent + 100) - *(float*)(DAT_80611d54 + 0x27c)),
                                                  (double)(*(float*)(ent + 0x6c) - *(float*)(DAT_80611d54 + 0x284)));
            float aspectX = (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615738) / FLOAT_806158dc;
            uint pi2 = (uint)(FLOAT_806157c0 *
                ((FLOAT_80615860 + (float)bearing) - (FLOAT_806157c4 * FLOAT_80612818) / FLOAT_80615848) + FLOAT_80615734);
            int bx = (int)(aspectX * (float)(r * (double)(float)(&DAT_80583228)[pi2 & 0x7ff]));
            float aspectRatio = ((float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_80615738) /
                                 (float)((double)CONCAT44(0x43300000, DAT_8060f954) - DOUBLE_80615740)) /
                                ((float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_80615738) /
                                 (float)((double)CONCAT44(0x43300000, DAT_8060f950) - DOUBLE_80615740));
            int by = (int)((float)(r * (double)*(float*)((int)&DAT_80583228 + ((pi2 + 0x200) * 4 & 0x1ffc))) * aspectX * aspectRatio);
            FUN_80170410(bx + (cx - 2), by + (cy - 2), bx + cx + 2, by + cy + 2, 0, 0, 0x50, 0x50);
        }
        off += 4;
    }
    FUN_80382724();
    return;
}
