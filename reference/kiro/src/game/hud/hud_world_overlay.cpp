// Batch 339 | Date: 2026-09-04
// HUD world-space overlay group: screen flash, floating world text messages,
// and player nametags. All three are driven each frame from
// hud_render_player_view.
//
//   Screen flash (full-screen white/black fade bars):
//     0x8011d46c hud_screen_flash_tick     (advance the flash timer)
//     0x8011d4e4 hud_screen_flash_render   (draw top/bottom fade bars)
//
//   World-space floating text (rising pop-up messages, e.g. score/pickup):
//     0x8011d660 hud_worldmsg_stub_a       (empty stub)
//     0x8011d664 hud_worldmsg_push         (push a message into the 20-slot ring)
//     0x8011d710 hud_worldmsg_reset        (clear the ring)
//     0x8011d71c hud_worldmsg_tick         (rise + expire the messages)
//     0x8011d820 hud_worldmsg_render       (project to screen + draw shadowed text)
//
//   Player nametags (floating names + off-screen direction arrows):
//     0x8011daf8 hud_nametag_stub          (empty stub)
//     0x8011dafc hud_nametag_system_alloc  (per-player 0x80 buffers)
//     0x8011db84 hud_nametag_system_reset  (zero the per-player buffers)
//     0x8011dc0c hud_nametag_render        (per-player nametag + arrow render)
//
//   0x8011d430 hud_overlay_stub_loop       (empty player-count loop)
//
// Subsystem: game (HUD rendering)

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================

// scalar float constants
extern float FLOAT_80615718;   // 1.0
extern float FLOAT_8061572c;
extern float FLOAT_80615730;   // 0.0
extern float FLOAT_80615734;   // 0.5
extern float FLOAT_80615748;   // 2.0
extern float FLOAT_80615750;
extern float FLOAT_8061575c;
extern float FLOAT_80615760;
extern float FLOAT_80615768;
extern float FLOAT_8061577c;
extern float FLOAT_80615780;
extern float FLOAT_80615784;
extern float FLOAT_80615788;
extern float FLOAT_8061579c;
extern float FLOAT_806157b0;
extern float FLOAT_806157b4;
extern float FLOAT_806157c0;
extern float FLOAT_806157c4;
extern float FLOAT_806157f0;
extern float FLOAT_80615814;
extern float FLOAT_80615834;
extern float FLOAT_80615848;
extern float FLOAT_80615860;
extern float FLOAT_806158b8;
extern float FLOAT_80615948;
extern float FLOAT_8061082c;
extern float FLOAT_80610830;
extern float FLOAT_806108ec;
extern float FLOAT_80611674;

extern double DOUBLE_80615738; // 0x4330..0 unsigned int->double bias

// global data
extern int*  DAT_80611748;      // viewport rect (int[10])
extern int   DAT_80611d54;      // active view context base
extern int*  DAT_80611d74;      // current player-view pointer
extern int   DAT_80611d78;      // player-slot table base
extern uint  DAT_806109c0;      // render width
extern int   DAT_806108e4;      // world-msg rise speed (per frame)
extern uint  DAT_805015c0;      // feature-enable flags (bit0 = nametags)
extern int   DAT_80612098;      // tracked-marker count
extern int   DAT_80612610;      // marker mode threshold
extern int   DAT_80610864;      // localization/format context

extern unsigned char DAT_80611f9c; // world-msg active count (0..20)

// world-message ring buffer (20 slots, 0x14 stride):
//   +0x00 s16 stringId        (DAT_80582314)
//   +0x02 s16 lifetime        (DAT_80582316)
//   +0x04 f32 pos.x           (DAT_80582318)
//   +0x08 f32 pos.y           (DAT_8058231c)
//   +0x0c f32 pos.z           (DAT_80582320)
//   +0x10 f32 riseOffset      (DAT_80582324)
extern short DAT_80582314;
extern short DAT_80582316;
extern float DAT_80582318;
extern float DAT_8058231c;
extern float DAT_80582320;
extern float DAT_80582324;
extern int   DAT_80582328;      // ring-shift helper arg
extern int   DAT_80582314b;     // (guard; not referenced)

// per-player nametag scratch buffers (indexed by player*4)
extern int   DAT_805824a4;      // arrow/timer buffer A pointers
extern int   DAT_805824b4;      // fade buffer B pointers

// tracked-entity marker table
extern int   DAT_80588610;      // marker record pointers (int[])
extern int*  DAT_80610a20;      // texture descriptor table base
extern float DAT_80583228;      // sin/cos LUT
extern int   DAT_803f2c7c;      // per-team name color table (5-word stride)

// helpers (raw)
extern int    FUN_801c1714(void);              // active player count
extern int    FUN_803826e0(void);              // HUD state push (variant)
extern int    FUN_8038272c(void);              // HUD state pop (variant)
extern int    FUN_803826ec(void);              // HUD state push (variant)
extern int    FUN_80382738(void);              // HUD state pop (variant)
extern int    FUN_80382718(void);              // HUD state pop (variant)
extern unsigned long long FUN_803826cc(void);  // HUD state push (variant)
extern int    FUN_8016f00c(int mode);
extern int    FUN_8016e84c(void);
extern int    FUN_8016e77c(int texId);
extern int    FUN_8016f4bc(int x0, int y0, int x1, int y1, int color); // solid rect
extern int    FUN_80387c0c(int a, int b, int c);          // ring shift-down
extern int    FUN_8012fa34(float* mtx, float* vec);       // world->view transform
extern int    FUN_8012fb70(float* mtx, float* vec, float* out); // view->screen project
extern int    FUN_8012f2c4(double x, double y, double z, double yaw, float* outMtx);
extern int    FUN_80130bd4(float* boneMtx, float* inMtx, float* outMtx);
extern int    FUN_80131538(double x);          // sqrt-ish curve
extern int    FUN_803825c0(double v);          // float -> clamped color byte
extern int    mem_alloc(unsigned long long, ...);
extern int    memset_wrapper(int dst, int val, int len);
extern int    scene_raycast(int scene, int* from, float* to, int, float*, float*, int*, int, int, int*);
extern int    player_is_scoped_or_mounted(int slot);
extern double pickup_fade_get_alpha(int fadeState);
extern int    sprite_get_texture_id(int spriteId);
extern int    text_set_font_table(int table);
extern unsigned char* FUN_8012e878(long long, double, double, double, double, double,
                                   unsigned long long, unsigned long long, int* fmt,
                                   int arg, uint, uint, uint, int*, uint, uint); // format text
extern unsigned char* FUN_80365eb4(double, double, double, double, double, double, double, double); // resolve name string
extern int    text_get_dimensions_int(int* w, int* h, unsigned char* str);
extern int    text_measure_string(float* w, float* h, unsigned char* str, int flags);
extern double text_get_char_width(unsigned char* str);
extern int    text_apply_alpha_fade(double a, unsigned char* str, int, int);
extern int    text_set_color_rgb(double r, double g, double b);
extern int    text_draw_char_shadowed(double x, double y, unsigned long long str, uint c0, uint c1);
extern int    FUN_80170b10(double, double, double, double, int, int, uint, uint); // textured quad

// ============================================================================
// 0x8011d430 | hud_overlay_stub_loop
// An empty loop over the active player count (leftover / no-op).
// ============================================================================
extern "C" void hud_overlay_stub_loop(void) {
    for (int i = 0; i < FUN_801c1714(); i = i + 1) {
    }
    return;
}

// ============================================================================
// 0x8011d46c | hud_screen_flash_tick
// Advances the full-screen flash timer (state+0x170c) by a frame-scaled rate
// while active (state+0x1708 != 0), disabling the flash once it reaches 1.0.
// state: the per-view flash state block.
// ============================================================================
extern "C" void hud_screen_flash_tick(int state) {
    float one = FLOAT_80615718;
    if (*(int*)(state + 0x1708) != 0) {
        *(float*)(state + 0x170c) =
            FLOAT_80615948 * FLOAT_806108ec *
                (FLOAT_80615718 /
                 (float)((double)(longlong)(uint)DAT_806109c0 - (double)0.0)) +
            *(float*)(state + 0x170c);
        if (one <= *(float*)(state + 0x170c)) {
            *(int*)(state + 0x1708) = 0;
            *(float*)(state + 0x170c) = FLOAT_80615730;
        }
    }
    return;
}

// ============================================================================
// 0x8011d4e4 | hud_screen_flash_render
// Draws the active screen flash as a pair of fade rectangles growing from the
// top and bottom of the viewport (a "closing eyelid" flash). The bar height is
// driven by a triangle-wave of the flash timer.
// state: per-view flash state block.
// ============================================================================
extern "C" void hud_screen_flash_render(int state) {
    if (*(int*)(state + 0x1708) != 0) {
        double t = (double)(float)((double)FLOAT_80615748 * (double)*(float*)(state + 0x170c));
        if ((double)FLOAT_80615718 < t) {
            t = (double)(float)((double)FLOAT_80615748 - t);   // triangle wave
        }
        uint vpH = DAT_80611748[8];
        double barH = (double)(float)((double)(FLOAT_80615734 *
                          (float)((double)(longlong)(int)vpH - (double)0.0)) * t);
        FUN_8016f00c(1);
        // dim tint over the whole viewport
        FUN_8016f4bc(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6],
                     (int)((double)FLOAT_80615750 * t) | 0x7f7f7f00);
        // top black bar
        FUN_8016f4bc(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1],
                     (int)((double)(float)((double)(longlong)(int)DAT_80611748[5] - (double)0.0) + barH),
                     0x7f000000);
        // bottom black bar
        FUN_8016f4bc(DAT_80611748[0],
                     (int)((float)((double)(longlong)(int)DAT_80611748[5] - (double)0.0) +
                           (float)((double)(float)((double)(longlong)(int)vpH - (double)0.0) - barH)),
                     DAT_80611748[1], DAT_80611748[6], 0x7f000000);
    }
    return;
}

// ============================================================================
// 0x8011d660 | hud_worldmsg_stub_a  (empty)
// ============================================================================
extern "C" void hud_worldmsg_stub_a(void) { return; }

// ============================================================================
// 0x8011d664 | hud_worldmsg_push
// Pushes a new floating world-text message into the 20-slot ring at
// DAT_80582314. If the ring is full (count == 20) the oldest is shifted out
// first. Stores string id, initial lifetime (2*render width), world position,
// and a zeroed rise offset.
// pos: float[3] world position. stringId: message string id.
// ============================================================================
extern "C" void hud_worldmsg_push(int* pos, unsigned short stringId) {
    if (DAT_80611f9c == 0x14) {
        FUN_80387c0c((int)&DAT_80582328, (int)&DAT_80582314, 0);
        DAT_80611f9c = DAT_80611f9c - 1;
    }
    float zero = FLOAT_80615730;
    uint idx = (uint)DAT_80611f9c;
    int life = DAT_806109c0 << 1;
    (&DAT_80582314)[idx * 10] = (short)stringId;
    (&DAT_80582316)[idx * 10] = (short)life;
    (&DAT_80582324)[idx * 5] = zero;
    (&DAT_80582318)[idx * 5] = *(float*)pos;
    (&DAT_8058231c)[idx * 5] = *(float*)(pos + 1);
    (&DAT_80582320)[idx * 5] = *(float*)(pos + 2);
    DAT_80611f9c = DAT_80611f9c + 1;
    return;
}

// ============================================================================
// 0x8011d710 | hud_worldmsg_reset
// Clears the floating-message ring (count = 0).
// ============================================================================
extern "C" void hud_worldmsg_reset(void) {
    DAT_80611f9c = 0;
    return;
}

// ============================================================================
// 0x8011d71c | hud_worldmsg_tick
// Per-frame: decays each message's lifetime, raises its screen offset, and
// removes any that have expired (shifting the ring down and re-testing the
// same index).
// ============================================================================
extern "C" void hud_worldmsg_tick(void) {
    FUN_803826ec();
    double riseScale = (double)FLOAT_80615780;
    double bias = DOUBLE_80615738;
    for (int i = 0; i < (int)(uint)DAT_80611f9c; i = i + 1) {
        uint speedNeg = DAT_806108e4 ^ 0x80000000;
        uint widthx2  = DAT_806109c0 << 1;
        (&DAT_80582316)[i * 10] = (&DAT_80582316)[i * 10] - (short)DAT_806108e4;
        (&DAT_80582324)[i * 5] =
            (float)(riseScale * (double)((float)((double)CONCAT44(0x43300000, speedNeg) - bias) /
                                         (float)((double)CONCAT44(0x43300000, widthx2 ^ 0x80000000) - bias)) +
                    (double)(float)(&DAT_80582324)[i * 5]);
        if ((short)(&DAT_80582316)[i * 10] < 1) {
            FUN_80387c0c((int)&DAT_80582328, (int)&DAT_80582314, 0);
            i = i + -1;
            DAT_80611f9c = DAT_80611f9c - 1;
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8011d820 | hud_worldmsg_render
// Projects each floating message from world space to screen space, and (when
// it lands inside the viewport and in front of the camera) draws the localized
// string as centered shadowed text, tinted red/green by the sign of the
// message id and faded by remaining lifetime.
// ============================================================================
extern "C" void hud_worldmsg_render(void) {
    FUN_803826e0();
    int slotOff = 0;
    for (int i = 0; i < (int)(uint)DAT_80611f9c; i = i + 1) {
        if (*(short*)((int)&DAT_80582316 + slotOff) != 0) {
            float world[3];
            float screen[5];
            world[0] = *(float*)((int)&DAT_80582318 + slotOff);
            world[1] = *(float*)((int)&DAT_8058231c + slotOff) +
                       *(float*)((int)&DAT_80582324 + slotOff);
            world[2] = *(float*)((int)&DAT_80582320 + slotOff);
            FUN_8016e84c();
            FUN_8012fa34(*(float**)(DAT_80611d54 + 0x208), world);
            FUN_8012fb70(*(float**)(DAT_80611d54 + 0x204), world, screen);
            float sx = screen[0], sy = screen[1], sw = screen[3];
            (void)screen[2];
            if ((-sw < sw) && (sw != 0.0f)) {
                double xf = (double)(sx / sw);
                double yf = (double)(sy / sw);
                (void)xf; (void)yf;
                // NB: exact projection math preserved via the projected
                // screen[] components; here we compute integer pixel centre.
                double vpX2 = (double)(longlong)(int)DAT_80611748[2];
                double vpX4 = (double)(longlong)(int)DAT_80611748[4];
                double vpY7 = (double)(longlong)(int)DAT_80611748[7];
                double vpY9 = (double)(longlong)(int)DAT_80611748[9];
                int px = (int)((float)vpX4 * (float)xf + (float)vpX2);
                int py = (int)-(float)((float)vpY9 * (float)yf - (float)vpY7);

                FUN_8016f00c(2);
                text_set_font_table(3);
                unsigned char* str = FUN_8012e878((long long)px, (double)0.0, (double)px,
                                                  yf, (double)vpX4, DOUBLE_80615738,
                                                  0, 0, &DAT_80610864,
                                                  (int)*(short*)((int)&DAT_80582314 + slotOff),
                                                  0, 0, 0, DAT_80611748, 0, 0);
                int tw, th;
                text_get_dimensions_int(&tw, &th, str);
                int cx = px - (tw >> 1);
                int cy = py - (th >> 1);
                if ((DAT_80611748[0] < cx) && (cx < DAT_80611748[1]) &&
                    (DAT_80611748[5] < cy) && (cy < DAT_80611748[6])) {
                    short sid = *(short*)((int)&DAT_80582314 + slotOff);
                    short life = *(short*)((int)&DAT_80582316 + slotOff);
                    uint widthx2 = DAT_806109c0 << 1;
                    uint fade = (uint)((double)FLOAT_80615750 *
                                       (double)(float)((double)(float)((double)(longlong)(int)life - (double)0.0) /
                                                       (double)(float)((double)(longlong)(int)(widthx2) - (double)0.0)));
                    uint cFill, cShadow;
                    if (sid < 0) {
                        cShadow = fade | 0x50000000;
                        cFill   = fade | 0x7f7f4000;
                    } else {
                        cShadow = fade | 0x5000;
                        cFill   = fade | 0x40696900;
                    }
                    unsigned char* str2 =
                        FUN_8012e878((long long)life, DOUBLE_80615738, (double)FLOAT_80615750,
                                     yf, (double)vpX4, DOUBLE_80615738, 0, 0, &DAT_80610864,
                                     (int)sid, 0, (uint)(int)life ^ 0x80000000, 0, DAT_80611748, 0, 0);
                    text_draw_char_shadowed((double)cx, (double)cy,
                                            (unsigned long long)(uintptr_t)str2, cFill, cShadow);
                }
            }
            FUN_8016e84c();
        }
        slotOff = slotOff + 0x14;
    }
    FUN_8038272c();
    return;
}

// ============================================================================
// 0x8011daf8 | hud_nametag_stub  (empty)
// ============================================================================
extern "C" void hud_nametag_stub(void) { return; }

// ============================================================================
// 0x8011dafc | hud_nametag_system_alloc
// When nametags are enabled (DAT_805015c0 bit0), allocates two 0x80-byte
// scratch buffers per active player (arrow-timer buffer A + fade buffer B).
// ============================================================================
extern "C" void hud_nametag_system_alloc(void) {
    unsigned long long ctx = FUN_803826ec();
    if ((DAT_805015c0 & 1) != 0) {
        int i = FUN_801c1714();
        while (true) {
            i = i + -1;
            if (i < 0) break;
            *(int*)(&DAT_805824a4 + i * 4) = mem_alloc(ctx, 0x80, 1, 0x80580000);
            *(int*)(&DAT_805824b4 + i * 4) = mem_alloc(ctx, 0x80, 1, 0x80580000);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8011db84 | hud_nametag_system_reset
// Zeroes the per-player nametag scratch buffers.
// ============================================================================
extern "C" void hud_nametag_system_reset(void) {
    FUN_803826ec();
    if ((DAT_805015c0 & 1) != 0) {
        int i = FUN_801c1714();
        while (true) {
            i = i + -1;
            if (i < 0) break;
            memset_wrapper(*(int*)(&DAT_805824a4 + i * 4), 0, 0x80);
            memset_wrapper(*(int*)(&DAT_805824b4 + i * 4), 0, 0x80);
        }
    }
    FUN_80382738();
    return;
}

// ============================================================================
// 0x8011dc0c | hud_nametag_render
// Per-frame player-nametag overlay. For each tracked entity marker
// (DAT_80588610[]): skips the local player, does an LOS raycast to decide
// occlusion, projects the head position to screen space, ramps arrow/fade
// timers, and draws the floating name text plus (when off-screen or occluded)
// a direction arrow sprite (0xeb / 0xed) pointing toward the target.
// Only runs when nametags are enabled and a valid current player-view exists.
// ============================================================================
extern "C" void hud_nametag_render(void) {
    double f1 = (double)FUN_803826cc();
    if (((DAT_80611d74 != (int*)0) && (-1 < DAT_80611d74[1]) && (DAT_80611d74[5] != 0)) &&
        ((DAT_805015c0 & 1) != 0)) {
        int playerOff = DAT_80611d74[1] * 4;
        int bufA = *(int*)(&DAT_805824a4 + playerOff);   // arrow/scroll timer
        int bufB = *(int*)(&DAT_805824b4 + playerOff);   // fade timer

        int idx = DAT_80612098;
        while (idx = idx + -1, -1 < idx) {
            int slot = idx * 4;
            uint visible = 1;
            int* rec = (int*)(&DAT_80588610)[idx];
            if ((rec != (int*)0) && (rec[0] != 0) &&
                ((rec[2] != *DAT_80611d74) || ((*(uint*)(DAT_80611d54 + 0x21c) & 4) == 0))) {

                int ent = rec[0];
                // LOS / visibility test (only when not the same weapon owner)
                if (rec[0x21] != *(int*)(*(int*)(DAT_80611d74[5] + 0x7c) + 0x84)) {
                    if ((*(uint*)(ent + 0xc) & 1U << *(int*)(DAT_80611d54 + 0x214)) == 0) {
                        visible = 0;
                    } else {
                        int model = *(int*)(ent + 4);
                        if ((model == 0) || (*(int*)(model + 0x84) == 0) ||
                            (*(int*)(model + 0x10) == 0)) {
                            visible = 0;
                        } else {
                            float head[3];
                            head[0] = *(float*)(ent + 0x6c) - (float)DAT_80611d74[0x2d];
                            head[1] = 0.0f;
                            head[2] = 0.0f;
                            (void)head;
                            int hit = scene_raycast(DAT_80611d74[0x37b], DAT_80611d74 + 0x2d,
                                                    head, 0, (float*)0, (float*)0, (int*)0,
                                                    0x2010278, 1, (int*)0);
                            visible = (uint)(hit != 0) ? 0 : 1;
                        }
                    }
                }

                unsigned char* nameStr;
                uint scoped;
                float wpos[3];
                if ((rec == (int*)0) || (DAT_80612610 <= rec[2])) {
                    nameStr = (unsigned char*)rec[0x1c];
                    scoped = (uint)(*(float*)(rec[0] + 0x1cc) <= FLOAT_80615730);
                    wpos[0] = *(float*)(ent + 100);
                    wpos[1] = *(float*)(ent + 0x68);
                    wpos[2] = *(float*)(ent + 0x6c);
                } else {
                    nameStr = FUN_80365eb4(f1, 0, 0, 0, 0, 0, 0, 0);
                    scoped = player_is_scoped_or_mounted(DAT_80611d78 + rec[2] * 0x141c);
                    int st = *(int*)(ent + 0x150);
                    wpos[0] = *(float*)(st + 0xa8);
                    wpos[1] = *(float*)(st + 0xac);
                    wpos[2] = *(float*)(st + 0xb0);
                }

                float dx = *(float*)(DAT_80611d54 + 0x27c) - wpos[0];
                float dy = *(float*)(DAT_80611d54 + 0x280) - wpos[1];
                float dz = *(float*)(DAT_80611d54 + 0x284) - wpos[2];
                double dist = (double)FUN_80131538((double)(dz * dz + dx * dx + dy * dy));
                float lift = (scoped != 0) ? FLOAT_80615730 : FLOAT_80615718;
                wpos[1] = wpos[1] + FLOAT_80615718 + lift;

                // ramp the per-marker arrow timer (bufA[idx]) up or down with distance
                float* aTimer = (float*)(bufA + slot);
                float rate = FLOAT_806108ec *
                             (FLOAT_80615718 /
                              (float)((double)(longlong)(uint)(DAT_806109c0 ^ 0x80000000) - (double)0.0));
                (void)rate;
                if ((double)FLOAT_80615760 <= dist) {
                    if (*aTimer > 0.0f) {
                        float v = *aTimer - FLOAT_80615768 * FLOAT_806108ec;
                        if (v < 0.0f) v = 0.0f;
                        *aTimer = v;
                    }
                } else {
                    float v = *aTimer + FLOAT_80615768 * FLOAT_806108ec;
                    if (v > FLOAT_80615718) v = FLOAT_80615718;
                    *aTimer = v;
                }

                // ramp the per-marker fade timer (bufB[idx])
                float* fTimer = (float*)(bufB + slot);
                if (visible == 0) {
                    float v = *fTimer - FLOAT_80615780 * FLOAT_806108ec;
                    if (v < 0.0f) v = 0.0f;
                    *fTimer = v;
                } else {
                    float v = *fTimer + FLOAT_80615780 * FLOAT_806108ec;
                    if (v > FLOAT_80615718) v = FLOAT_80615718;
                    *fTimer = v;
                }

                // draw the nametag + arrow when near enough and faded in
                if ((dist <= (double)FLOAT_806157b0) && ((double)FLOAT_806158b8 <= (double)*fTimer)) {
                    float vpos[3] = { wpos[0], wpos[1], wpos[2] };
                    float scr[5];
                    FUN_8012fa34(*(float**)(DAT_80611d54 + 0x208), vpos);
                    FUN_8012fb70(*(float**)(DAT_80611d54 + 0x204), vpos, scr);
                    float w = scr[3];
                    float h = scr[1];
                    if ((-w < h) && (h < w)) {
                        int tex = sprite_get_texture_id(0xeb);
                        int desc = *DAT_80610a20 + tex * 0x58;
                        uint tw = (uint)*(unsigned short*)(desc + 0x4e);
                        uint th = (uint)*(unsigned short*)(desc + 0x52);

                        // distance-based text alpha (near/far fade envelope)
                        float envelope;
                        if (dist <= (double)FLOAT_80615834) {
                            envelope = FLOAT_80615814;
                            if ((dist <= (double)FLOAT_80615784) && ((double)FLOAT_80615788 < dist)) {
                                double u = (double)((float)((double)FLOAT_80615784 - dist) / FLOAT_8061579c);
                                if (u > (double)FLOAT_80615718) u = (double)FLOAT_80615718;
                                if (u < 0.0) u = 0.0;
                                envelope = (float)((double)FLOAT_8061572c * u + (double)FLOAT_80615814);
                            }
                        } else {
                            double u = (double)((float)((double)FLOAT_806157b0 - dist) / FLOAT_8061579c);
                            if (u > (double)FLOAT_80615718) u = (double)FLOAT_80615718;
                            if (u < 0.0) u = 0.0;
                            envelope = (float)((double)FLOAT_80615814 * u);
                        }

                        double alpha = (double)(envelope * *fTimer);
                        double fadeA = pickup_fade_get_alpha((int)(intptr_t)(rec + 0x31a));
                        alpha = (double)(float)(alpha * fadeA);
                        uint aByte = FUN_803825c0((double)(float)((double)FLOAT_806157f0 * alpha));
                        uint teamCol = (&DAT_803f2c7c)[rec[0x21] * 5];

                        FUN_8016f00c(1);
                        FUN_8016e84c();

                        // screen pixel centre from projected coords
                        double cx = (double)(scr[0] / scr[3]);
                        double cy = (double)(scr[2] / scr[3]);
                        (void)cy;
                        text_set_font_table(0x13);

                        if (scoped == 0) {
                            if (nameStr != (unsigned char*)0) {
                                text_apply_alpha_fade((double)(float)(255.0f), nameStr, 0, 1);
                                float mw, mh;
                                text_measure_string(&mw, &mh, nameStr, 0);
                                double tx = (double)(float)((float)cx - 0.5f * mw);
                                double ty = (double)(float)((float)cy - 0.5f * mh);
                                uint aByte2 = FUN_803825c0((double)(float)((double)FLOAT_806157b4 * alpha));
                                text_draw_char_shadowed(tx, ty,
                                                        (unsigned long long)(uintptr_t)nameStr,
                                                        aByte | (teamCol & 0xffffff00),
                                                        aByte2 & 0xff);
                                text_set_color_rgb((double)FLOAT_80615718,
                                                   (double)FLOAT_80615718,
                                                   (double)FLOAT_80615718);
                            }
                        } else {
                            tex = sprite_get_texture_id(0xed);
                        }

                        // off-screen / occluded direction arrow quad
                        double arrowY = cy;
                        double half = (double)((FLOAT_80610830 * (float)th) / FLOAT_8061579c);
                        FUN_8016e77c(tex);
                        FUN_80170b10(cx, arrowY - half, cx, arrowY + half,
                                     0, 0, tw << 4, th << 4);
                        FUN_8016e84c();
                    }
                }
            }
        }
    }
    FUN_80382718();
    return;
}
