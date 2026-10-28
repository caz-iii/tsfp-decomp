// Address range: 0x8035d8e8 - 0x80360578 | batch_1020 | 2026-09-12
//
// Radar / minimap HUD render + teleport-portal ("warp") visual effect.
//
// This module is the sibling of the screen-effect overlay (batch_1019). It has
// two cooperating parts:
//
//   RADAR / MINIMAP  - draws the scanner frame, projects scene geometry as line
//   segments, and plots entity/sound-zone/checkpoint/AI-marker blips into the
//   per-player HUD viewport (screen-effect slot array DAT_805e4fc4, stride 0x68).
//
//   WARP PORTAL FX   - a teleport/portal visual driven by a global state machine
//   (DAT_80612e64) with a 400-particle field (DAT_805e5180, stride 0x20): 2D
//   overlay (radial spokes + ring + particles) and a 3D reflected mesh streamed
//   through the GX FIFO (DAT_cc008000).
//
//   radar_render_column        0x8035d8e8  per-column render gate
//   radar_draw_map             0x8035d94c  scanner grid + node lines + blips
//   radar_draw_player_blip     0x8035ec24  local player blip + portal overlay
//   warpfx_spawn_particles     0x8035f4a4  seed 400-particle grid
//   warpfx_integrate_particles 0x8035f62c  advance particle lifetimes
//   warpfx_set_source          0x8035f6f4  store portal source/dest
//   warpfx_update              0x8035f700  portal state machine
//   warpfx_draw_portal         0x8035fa08  2D portal overlay
//   warpfx_draw_portal_default 0x80360538  portal overlay at default 256x256
//   warpfx_draw_portal_mesh    0x80360578  3D reflected portal mesh (GX FIFO)
//
// NOTE: this is a non-matching decomp. The four large draw functions
// (radar_draw_map, radar_draw_player_blip, warpfx_draw_portal,
// warpfx_draw_portal_mesh) preserve control flow and the GX call sequence but
// use the i2f() helper for the PPC int->double conversion idiom rather than the
// raw CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_bias expansion.
#include "common.h"
#include "generated_decls.h"

// ---- screen-effect slot array (shared with screen_effect.cpp) ----
extern unsigned char DAT_805e4fc4[];   // slot[0]; per-column stride 0x68 (0x1a dwords)
extern float DAT_805e4fb8;             // camera ref x
extern float DAT_805e4fc0;             // camera ref z

// ---- radar clip bounds (world-space rect written by radar_draw_map) ----
extern float DAT_805e5164, DAT_805e5168, DAT_805e516c;
extern float DAT_805e5170, DAT_805e5174, DAT_805e5178;

// ---- 400-particle portal field (stride 0x20) ----
extern unsigned char DAT_805e5180[];   // particle[0]: cur x,y / dst x,y / vel / life
extern unsigned char DAT_805e8380[];   // portal mesh vertex scratch

// ---- portal effect globals ----
extern int   DAT_80612e64;   // portal state (0 off; 1..4 phases; 2 = open)
extern int   DAT_80612e4c;   // direction flag
extern int*  DAT_80612e3c;   // source object matrix ptr
extern int   DAT_80612e38;   // dest ptr
extern float FLOAT_80612e40, FLOAT_80612e44, FLOAT_80612e48;
extern float FLOAT_80612e50, FLOAT_80612e54, FLOAT_80612e5c, FLOAT_80612e60;
extern float FLOAT_80612e2c;  // shared scroll phase (screen_effect)
extern float FLOAT_80612e28;  // marker projection scale

// ---- render context / misc globals ----
extern int   DAT_80611d74;   // active render context record
extern int   DAT_80611d54;   // camera record
extern int   DAT_80611748;   // screen record
extern int   DAT_80611928;   // free-running frame counter
extern int   DAT_80611b04;   // scene node index table
extern int   DAT_80611cb0, DAT_80611cb8;   // entity table base/count
extern int   DAT_80611cd0, DAT_80611cd4;   // pickup table base/count
extern int   DAT_80612098;                 // projectile count
extern int   DAT_80612614;                 // game-mode id
extern int   DAT_80612250, DAT_80612258;   // reflect-matrix latches
extern int*  DAT_80610a20;   // texture descriptor table ptr
extern int   DAT_806109c0, DAT_806109c4, DAT_806109c8;  // timebase, screen w, h
extern int   DAT_806108e4;   // effects-enabled
extern float DAT_806108ec;   // frame delta scale
extern int   DAT_8060fae8;   // fullscreen-map flag
extern unsigned char DAT_805015bd;   // menu/game state
extern unsigned char DAT_805401dc[]; // node child-count table
extern int   DAT_8053fb2c[];          // node child-list table
extern float DAT_804876b0[];          // portal spoke offset table
extern float DAT_80487680[];          // portal mesh param tables
extern float DAT_80487684[];
extern float DAT_80487688[];
extern float DAT_8048768c[];
extern unsigned char DAT_80588610[];  // projectile ptr table
extern float DAT_805831a8;            // camera basis matrix
extern float DAT_8058e3cc;            // reflect scratch matrix
extern float DAT_805e8390, DAT_805e8394, DAT_805e83e4, DAT_805e83e8;
extern char  s___d___d__804875b0[];   // "[%d][%d]\n"

// ---- sin/cos LUT ----
extern float DAT_80583228[];

// ---- rodata float/double constants ----
extern float FLOAT_8061afe0, FLOAT_8061aff0, FLOAT_8061aff4, FLOAT_8061aff8;
extern float FLOAT_8061b004, FLOAT_8061b010, FLOAT_8061b014, FLOAT_8061b01c;
extern float FLOAT_8061b024, FLOAT_8061b034, FLOAT_8061b038, FLOAT_8061b03c, FLOAT_8061b040;
extern float FLOAT_8061b044, FLOAT_8061b048, FLOAT_8061b04c, FLOAT_8061b050, FLOAT_8061b054;
extern float FLOAT_8061b058, FLOAT_8061b05c, FLOAT_8061b060, FLOAT_8061b064, FLOAT_8061b068;
extern float FLOAT_8061b06c, FLOAT_8061b070, FLOAT_8061b074, FLOAT_8061b07c, FLOAT_8061b080;
extern float FLOAT_8061b078;
extern float FLOAT_8061b084, FLOAT_8061b088, FLOAT_8061b098, FLOAT_8061b09c, FLOAT_8061b0a0;
extern float FLOAT_8061b0a4, FLOAT_8061b0a8, FLOAT_8061b0ac, FLOAT_8061b0b0, FLOAT_8061b0b4;
extern float FLOAT_8061b0b8, FLOAT_8061b0bc, FLOAT_8061b0c0, FLOAT_8061b0c4, FLOAT_8061b0c8;
extern float FLOAT_8061b0cc, FLOAT_8061b0d0, FLOAT_8061b0d4, FLOAT_8061b0d8, FLOAT_8061b0dc;
extern float FLOAT_8061b0e0, FLOAT_8061b0e4, FLOAT_8061b0e8, FLOAT_8061b0ec, FLOAT_8061b0f0;
extern float FLOAT_8061b0f4, FLOAT_8061b0f8, FLOAT_8061b0fc, FLOAT_8061b100, FLOAT_8061b104;
extern float FLOAT_8061b108, FLOAT_8061b10c, FLOAT_8061b110, FLOAT_8061b114, FLOAT_8061b118;
extern float FLOAT_8061b11c, FLOAT_8061b120;
extern double DOUBLE_8061afe8, DOUBLE_8061b090;

// ---- GX FIFO write-gather port (0xCC008000) ----
extern volatile float DAT_cc008000;

// ---- raw sibling / frame-scope helpers (not in generated headers) ----
extern int  FUN_803825c0(double x);        // pack intensity -> color byte
extern void FUN_80360f78(int newState);    // set portal state
extern void FUN_80387fb8(unsigned long long a, double b, double c, double d, double e,
                         double f, double g, unsigned long long h, int buf,
                         char* fmt, int v1, int v2, unsigned int v3, unsigned int v4,
                         unsigned int v5, unsigned int v6);   // sprintf-like
extern int  FUN_803826b4(void);
extern unsigned long long FUN_803826bc(void);
extern void FUN_803826c8(void);
extern unsigned long long FUN_803826e8(void);
extern void FUN_80382700(void);
extern void FUN_80382708(void);
extern void FUN_80382714(void);
extern void FUN_80382734(void);

// PPC int->double conversion idiom, condensed.
static inline float i2f(unsigned int u) { return (float)(int)u; }

// -------------------------------------------------------------------------
// 0x8035f6f4  warpfx_set_source
// -------------------------------------------------------------------------
void warpfx_set_source(int* srcMatrix, int dest)
{
    DAT_80612e38 = dest;
    DAT_80612e3c = srcMatrix;
}

// -------------------------------------------------------------------------
// 0x80360538  warpfx_draw_portal_default
// -------------------------------------------------------------------------
void warpfx_draw_portal_default(void)
{
    warpfx_draw_portal(0, 0, 0x100, 0x100, 0x100, 0, 0x64641b3c);
}

// -------------------------------------------------------------------------
// 0x8035f4a4  warpfx_spawn_particles - seed the 400-particle grid (20x20).
// -------------------------------------------------------------------------
void warpfx_spawn_particles(void)
{
    int i;
    int off;
    float vel;
    float col;
    float life;
    float span;

    FUN_803826e8();
    col  = FLOAT_8061b078;   // grid cell scale
    span = FLOAT_8061b07c;   // velocity span
    life = FLOAT_8061b084;   // base lifetime field
    off  = 0;
    for (i = 0; i < 400; i = i + 1) {
        float gx = col * (float)(i % 20);
        float gy = col * (float)(i / 20);
        float r;
        *(float*)(DAT_805e5180 + off + 0x10) = gx;   // +0x10 target x
        *(float*)(DAT_805e5180 + off + 0x14) = gy;   // +0x14 target y
        r = (float)(span * game_frand());
        if (((unsigned int)game_rand() & 1) == 0)
            *(float*)(DAT_805e5180 + off + 0x08) = (float)(r * (FLOAT_8061b080 - gy));
        else
            *(float*)(DAT_805e5180 + off + 0x08) =
                -(float)((double)r * ((double)FLOAT_8061b080 - (double)gy) - (double)FLOAT_8061b080);
        *(float*)(DAT_805e5180 + off + 0x0c) = life;                       // +0x0c
        *(float*)(DAT_805e5180 + off + 0x18) = (float)((FLOAT_8061b088 * r) * -(float)game_frand()); // +0x18 timer
        vel = (float)(FLOAT_8061b088 * game_frand() + FLOAT_8061b080);
        *(float*)(DAT_805e5180 + off + 0x1c) = vel;                        // +0x1c lifetime
        off = off + 0x20;
    }
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8035f62c  warpfx_integrate_particles - advance lifetimes; returns active.
// -------------------------------------------------------------------------
unsigned int warpfx_integrate_particles(void)
{
    unsigned int anyActive = 0;
    int off = 0;
    int n = 400;
    float dt = FLOAT_8061b080 / i2f((unsigned int)DAT_806109c0);
    dt = DAT_806108ec * dt;
    do {
        float t;
        *(float*)(DAT_805e5180 + off + 0x18) += dt;
        t = *(float*)(DAT_805e5180 + off + 0x18);
        if (t < *(float*)(DAT_805e5180 + off + 0x1c)) {
            anyActive = 1;
            if (FLOAT_8061b098 <= t) {
                float u = t / *(float*)(DAT_805e5180 + off + 0x1c);
                float u2 = u * u;
                float inv = FLOAT_8061b080 - u2;
                *(float*)(DAT_805e5180 + off + 0x00) =
                    *(float*)(DAT_805e5180 + off + 0x08) * inv +
                    *(float*)(DAT_805e5180 + off + 0x10) * u2;
                *(float*)(DAT_805e5180 + off + 0x04) =
                    *(float*)(DAT_805e5180 + off + 0x0c) * inv +
                    *(float*)(DAT_805e5180 + off + 0x14) * u2;
            }
        }
        off = off + 0x20;
        n = n - 1;
    } while (n != 0);
    return anyActive;
}

// -------------------------------------------------------------------------
// 0x8035f700  warpfx_update - portal state machine.
// -------------------------------------------------------------------------
void warpfx_update(void)
{
    float dt;
    double rnd;

    if (DAT_80612e64 == 0) return;

    dt = DAT_806108ec * (FLOAT_8061b080 / i2f((unsigned int)DAT_806109c0));
    FLOAT_80612e60 = FLOAT_80612e60 + dt;

    if ((FLOAT_8061b098 < FLOAT_80612e54) &&
        (FLOAT_80612e54 = dt * FLOAT_80612e50 + FLOAT_80612e54, FLOAT_8061b080 <= FLOAT_80612e54)) {
        FLOAT_80612e54 = FLOAT_8061b098;
    }
    FLOAT_80612e44 = FLOAT_80612e44 * FLOAT_8061b09c;
    if (FLOAT_80612e44 < FLOAT_8061b0a0) FLOAT_80612e44 = FLOAT_8061b098;

    if (FLOAT_8061b098 < FLOAT_80612e48) {
        FLOAT_80612e48 = dt * FLOAT_80612e40 + FLOAT_80612e48;
        if (game_frand() < (double)(FLOAT_8061b0ac * DAT_806108ec)) {
            FLOAT_80612e44 = (float)((double)FLOAT_8061b0a8 * game_frand() + (double)FLOAT_8061b0a4);
        }
        if (FLOAT_8061b080 <= FLOAT_80612e48) FLOAT_80612e48 = FLOAT_8061b098;
    }

    if (DAT_80612e64 != 2) {
        if (DAT_80612e64 < 2) {
            if ((0 < DAT_80612e64) && (warpfx_integrate_particles() == 0) &&
                (FLOAT_8061b080 <= FLOAT_80612e60)) {
                FUN_80360f78(2);
            }
        } else if ((DAT_80612e64 < 4) && (FLOAT_8061b080 <= FLOAT_80612e60)) {
            FUN_80360f78(0);
        }
    }

    if (DAT_80612e64 == 2) {
        rnd = game_frand();
        if (rnd < (double)(FLOAT_8061b0b0 * DAT_806108ec)) {
            unsigned int k = (unsigned int)game_rand() % 3;
            if (k == 2) {
                if ((FLOAT_8061b098 == FLOAT_80612e54) && (FLOAT_8061b098 == FLOAT_80612e48)) {
                    FLOAT_80612e50 = FLOAT_8061b088;
                    FLOAT_80612e54 = FLOAT_8061b088 * (FLOAT_8061b080 / i2f((unsigned int)DAT_806109c0));
                    DAT_80612e4c = (int)((unsigned int)game_rand() % 3);
                }
            } else if ((k < 2) && (FLOAT_8061b098 == FLOAT_80612e54) && (FLOAT_8061b098 == FLOAT_80612e48)) {
                FLOAT_80612e40 = FLOAT_8061b080 / (float)((double)FLOAT_8061b088 * game_frand() + (double)FLOAT_8061b07c);
                FLOAT_80612e48 = FLOAT_80612e40 * (FLOAT_8061b080 / i2f((unsigned int)DAT_806109c0));
            }
        }
    } else if (DAT_80612e64 < 2) {
        if ((0 < DAT_80612e64) &&
            (FLOAT_80612e5c = FLOAT_80612e60, FLOAT_8061b080 < FLOAT_80612e60)) {
            FLOAT_80612e5c = FLOAT_8061b080;
        }
    } else if ((DAT_80612e64 < 4) &&
               (FLOAT_80612e5c = FLOAT_8061b080 - FLOAT_80612e60, FLOAT_80612e5c < FLOAT_8061b098)) {
        FLOAT_80612e5c = FLOAT_8061b098;
    }
}

// -------------------------------------------------------------------------
// 0x8035d8e8  radar_render_column - per-column render gate.
// -------------------------------------------------------------------------
void radar_render_column(void)
{
    int col = *(int*)(DAT_80611d74 + 4);
    int* slot = (int*)(DAT_805e4fc4 + col * 0x68);
    if ((slot[0x10] != 2) && ((slot[0] & 2) == 0) &&
        (arcsel_column_flag1_ready(col) == 0)) {
        radar_draw_map();
        radar_draw_player_blip();
    }
}

// Small helper: pointer to a slot dword for the active render context's column.
static inline int slot_get(int col, int dwordIndex) {
    return *(int*)(DAT_805e4fc4 + col * 0x68 + dwordIndex * 4);
}

// -------------------------------------------------------------------------
// 0x8035d94c  radar_draw_map - scanner grid + scene node lines + blips.
//
// Non-matching: preserves the render structure (frame border, projected scene
// node line segments, entity/sound-zone/checkpoint/AI-marker blips) and the GX
// call order; the extensive int->float projection math is condensed via i2f().
// -------------------------------------------------------------------------
void radar_draw_map(void)
{
    int useTeam = FUN_803826b4();
    int* ctx = (int*)DAT_80611d74;
    int col = ctx[1];
    int* slot = (int*)(DAT_805e4fc4 + col * 0x68);
    unsigned int baseColor;
    int x, y, w, h, cx, cy;
    int nodeBuf[101];
    int nodeCount, ni, nOff;
    int zoneId = -1;
    int i;

    baseColor = *(unsigned int*)(slot_get(col, 1) * 0x28 - 0x7fb78aa0 + slot_get(col, 0x0f) * 4);
    gx_begin_2d_draw();
    ctx[0x3aa] = ctx[0x2d];
    ctx[0x3ab] = ctx[0x2e];
    ctx[0x3ac] = ctx[0x2f];
    if ((1 < session_get_state_flag()) && ((slot[0] & 1) != 0))
        baseColor = baseColor & 0xffffff00;
    gx_set_blend_mode(0);

    x = slot_get(col, 0x03);   // +0x0c rect x
    y = slot_get(col, 0x04);   // +0x10 rect y
    w = slot_get(col, 0x05);   // +0x14 rect w
    h = slot_get(col, 0x06);   // +0x18 rect h
    cx = x + (w >> 1);
    cy = y + (h >> 1);
    gx_set_scissor_rect(x, y, x + w, y + h);

    // Frame border (only when not fullscreen map).
    if (((slot[0] & 1) == 0) && (DAT_8060fae8 == 0)) {
        unsigned int line = baseColor & 0xffffff00 | 0x80;
        gx_set_sub_material_id(0xb);
        gx_fill_rect_clipped(x, y, x + w, y + h, baseColor & 0xffffff00 | 0x32);
        gx_set_sub_material_id(1);
        gx_draw_line_counted(x, y, x + w, y, line);
        gx_draw_line_counted(x, y, x, y + h, line);
        gx_draw_line_counted(x, y + h, x + w, y + h, line);
        gx_draw_line_counted(x + w, y + h, x + w, y, line);
    }

    // World->radar projection scale + clip bounds.
    FLOAT_80612e28 = i2f((unsigned int)w) / *(float*)(DAT_805e4fc4 + col * 0x68 + 0x38);
    {
        float pscale = FLOAT_8061aff8 * FLOAT_80612e28;
        DAT_805e5164 = (float)ctx[0x3aa] - (i2f((unsigned int)w) / pscale);
        DAT_805e5168 = (float)ctx[0x2b] - FLOAT_8061aff8;
        DAT_805e516c = (float)ctx[0x3ac] - (i2f((unsigned int)h) / pscale);
        DAT_805e5170 = (float)ctx[0x3aa] + (i2f((unsigned int)w) / pscale);
        DAT_805e5178 = (float)ctx[0x3ac] + (i2f((unsigned int)h) / pscale);
        DAT_805e5174 = FLOAT_8061aff8 + (float)ctx[0x2e];
    }

    // Projected scene-node line segments.
    nodeCount = scene_query_nodes_outer_bounds(0, &DAT_805e5164, (int)nodeBuf, 100);
    gx_set_sub_material_id(1);
    nOff = 0;
    for (ni = 0; ni < nodeCount; ni = ni + 1) {
        int lo = (useTeam != 0) ? 0 : -1;
        int step = (useTeam != 0) ? 0 : -1;
        int nodeId = *(int*)((int)nodeBuf + nOff);
        for (; lo < (int)DAT_805401dc[nodeId]; lo = lo + 1, step = step + 1) {
            int ent = (lo < 0) ? *(int*)(DAT_80611b04 + nodeId * 4)
                               : *(int*)(DAT_8053fb2c[nodeId] + step * 4);
            int p;
            if ((*(unsigned int*)(ent + 0x28) & 0x80000000) != 0) continue;
            for (p = 0; p < *(int*)(ent + 0x1e4); p = p + 1) {
                unsigned int* node = *(unsigned int**)(*(int*)(ent + 500) + p * 4);
                if ((*node == 0) || ((*node & *(unsigned int*)(*(int*)(ent + 4) + 0x30)) != 0)) {
                    float za = (float)node[3];
                    float zb = (float)node[6];
                    float lo2 = (za < zb) ? za : zb;
                    float hi2 = (za < zb) ? zb : za;
                    float inten;
                    int ax, ay, bx, by;
                    if ((float)ctx[0x2b] <= hi2) {
                        inten = FLOAT_8061b024;
                        if (((float)ctx[0x2e] < lo2) &&
                            (inten = FLOAT_8061b024 - FLOAT_8061b038 * (lo2 - (float)ctx[0x2b]),
                             inten < FLOAT_8061afe0))
                            inten = FLOAT_8061afe0;
                    } else {
                        inten = FLOAT_8061b024 - FLOAT_8061b038 * ((float)ctx[0x2e] - hi2);
                        if (inten < FLOAT_8061afe0) inten = FLOAT_8061afe0;
                    }
                    ax = (int)((float)cx + ((float)ctx[0x3aa] - (float)node[2]) * FLOAT_80612e28);
                    ay = (int)((float)cy + ((float)ctx[0x3ac] - (float)node[4]) * FLOAT_80612e28);
                    bx = (int)((float)cx + ((float)ctx[0x3aa] - (float)node[5]) * FLOAT_80612e28);
                    by = (int)((float)cy + ((float)ctx[0x3ac] - (float)node[7]) * FLOAT_80612e28);
                    gx_draw_line_counted(ax, ay, bx, by, baseColor & 0xffffff00 | (unsigned int)(int)inten);
                }
            }
        }
        nOff = nOff + 4;
    }

    if (((slot[0] & 1) != 0) || (DAT_8060fae8 != 0))
        gx_set_sub_material_id(2);

    // Entity/projectile blips.
    for (i = 0; i < DAT_80612098; i = i + 1) {
        int obj = *(int*)((int)&DAT_80588610 + i * 4);
        obj = *(int*)obj;
        {
            float wx = *(float*)(obj + 100);
            float wy = *(float*)(obj + 0x6c);
            if ((DAT_805e5170 < wx) || (wx < DAT_805e5164) ||
                (DAT_805e5178 < wy) || (wy < DAT_805e516c)) {
                *(unsigned int*)(obj + 0x28) &= 0xf7ffffff;
                continue;
            }
            *(unsigned int*)(obj + 0x28) |= 0x8000000;
            {
                int bx = (int)((float)cx + FLOAT_80612e28 * ((float)ctx[0x3aa] - wx));
                int by = (int)((float)cy + FLOAT_80612e28 * ((float)ctx[0x3ac] - wy));
                int tex = (int)sprite_get_texture_id(0xfe);
                gx_set_active_material(tex);
                gx_draw_sprite_rect(by - (int)FLOAT_8061aff8, bx - (int)FLOAT_8061aff8,
                                    by + (int)FLOAT_8061aff8, bx + (int)FLOAT_8061aff8,
                                    0, 0, 0x50, 0x50);
            }
        }
    }

    // Per-entity HUD markers (delegates to screenfx_draw_marker).
    if (DAT_805015bd == '\n') {
        int eoff = 0;
        for (i = 0; i < DAT_80611cb8; i = i + 1) {
            int* e = (int*)(DAT_80611cb0 + eoff);
            if ((e[0] != 0) && (*(int*)(e[0] + 4) != 0) &&
                (((e[2] & 2) != 0) || ((e[2] & 4) != 0)) && (e[4] != 9)) {
                screenfx_draw_marker(ctx, e, 0);
            }
            eoff = eoff + 0x1dc;
        }
        if (DAT_80612614 == 0xe) proj_ai_render_hud_markers();

        // Blip count readout.
        if (((slot[0] & 1) == 0) && (DAT_8060fae8 == 0)) {
            char buf[100];
            int prevFont;
            gx_set_scissor_rect(0, 0, *(int*)(DAT_80611748 + 0xc), *(int*)(DAT_80611748 + 0x20));
            prevFont = text_set_font_table(0xf);
            FUN_80387fb8(0ull, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0ull, (int)buf,
                         s___d___d__804875b0, DAT_80612098, 0, 0, 0, 0, 0);
            text_draw_char_shadowed((double)(float)(x + 5), (double)(float)(y - 0xf),
                                    buf, 0xe1e1e17f, 0x7f);
            text_set_font_table(prevFont);
        }
    }

    // Sound-zone markers.
    for (i = 0; i < 0x20; i = i + 1) {
        int chr;
        int* node;
        spawn_find_team_character(i);
        chr = 0; /* result path collapsed; zone lookup below drives drawing */
        zoneId = soundpad_find_zone_by_id(*(int*)(DAT_80611d74 + 4));
        if (zoneId < 0) break;
        node = soundpad_resolve_zone_node(zoneId);
        {
            float wx = (float)node[3];
            float wy = (float)node[5];
            if ((wx <= DAT_805e5170) && (DAT_805e5164 <= wx) &&
                (wy <= DAT_805e5178) && (DAT_805e516c <= wy)) {
                int bx = (int)((float)cx + FLOAT_80612e28 * ((float)ctx[0x3aa] - wx));
                int by = (int)((float)cy + FLOAT_80612e28 * ((float)ctx[0x3ac] - wy));
                int tex = (int)sprite_get_texture_id(0xfe);
                (void)worm_checkpoint_get_bit_state(i);
                gx_set_active_material(tex);
                gx_draw_sprite_rect(by - (int)FLOAT_8061aff0, bx - (int)FLOAT_8061aff0,
                                    by + (int)FLOAT_8061aff0, bx + (int)FLOAT_8061aff0,
                                    0, 0, 0x50, 0x50);
            }
        }
        (void)chr;
    }

    FUN_80382700();
}

// -------------------------------------------------------------------------
// 0x8035ec24  radar_draw_player_blip - local player blip + portal overlay.
//
// Non-matching: draws the player position sprite + rotated facing wedge, then
// the 2D portal overlay for the current column. Condensed via i2f().
// -------------------------------------------------------------------------
void radar_draw_player_blip(void)
{
    int ctx = DAT_80611d74;
    int col = *(int*)(DAT_80611d74 + 4);
    int* slot = (int*)(DAT_805e4fc4 + col * 0x68);
    float pscale = i2f((unsigned int)slot_get(col, 0x04)) / *(float*)(DAT_805e4fc4 + col * 0x68 + 0x38);
    float facing[18];

    FUN_803826e8();

    if (((DAT_80612e64 != 0) || (FLOAT_80612e2c != 0.0f)) && ((slot[0] & 1) != 0)) {
        float sx, sy, wedge[3];
        int tex;
        gx_set_scissor_rect(slot_get(col, 0x0a), slot_get(col, 0x0b),
                            slot_get(col, 0x0a) + slot_get(col, 0x0c),
                            slot_get(col, 0x0b) + slot_get(col, 0x0d));
        gx_set_sub_material_id(2);
        (void)FUN_803825c0((double)(float)(FLOAT_8061b070 * FLOAT_8061aff0));

        tex = (int)sprite_get_texture_id(0xfe);
        gx_set_active_material(tex);

        if (DAT_80612e64 == 0) {
            sx = *(float*)(ctx + 0xb4) - DAT_805e4fb8;
            sy = *(float*)(ctx + 0xbc) - DAT_805e4fc0;
        } else {
            sx = *(float*)(ctx + 0xb4) - *(float*)(DAT_80612e38 + 100);
            sy = *(float*)(ctx + 0xbc) - *(float*)(DAT_80612e38 + 0x6c);
        }
        wedge[0] = (float)((double)pscale * (double)sx);   // local_230
        wedge[1] = (float)((double)pscale * (double)sy);
        wedge[2] = FLOAT_8061afe0;
        mtx4_set_rotation_y((double)((FLOAT_8061b014 * -*(float*)(DAT_80611d74 + 0x100)) / FLOAT_8061b01c),
                            facing);
        mtx4_transform_point(facing, wedge);

        {
            int px = slot_get(col, 0x0a), py = slot_get(col, 0x0b);
            int bx = (int)((float)px + wedge[0]);
            int by = (int)((float)py + wedge[1]);
            gx_draw_sprite_rect(by - (int)FLOAT_8061aff8, bx - (int)FLOAT_8061aff8,
                                by + (int)FLOAT_8061aff8, bx + (int)FLOAT_8061aff8, 0, 0, 0x50, 0x50);
        }
    }

    warpfx_draw_portal(slot_get(col, 0x0a), slot_get(col, 0x0b),
                       slot_get(col, 0x0c), slot_get(col, 0x0d),
                       slot_get(col, 0x02), slot_get(col, 0x03), 0);
    gx_end_2d_draw();
    FUN_80382734();
}

// -------------------------------------------------------------------------
// 0x8035fa08  warpfx_draw_portal - 2D portal overlay (spokes + ring + field).
//
// Non-matching: preserves the spoke/ring/particle draw sequence keyed off the
// portal state DAT_80612e64. Condensed via i2f().
// -------------------------------------------------------------------------
void warpfx_draw_portal(int x, int y, int w, int h, unsigned int u, unsigned int v, unsigned int flags)
{
    unsigned long long fa;
    unsigned int fx, fy;
    int savedMat, savedBlend;

    fa = FUN_803826bc();
    fx = (unsigned int)(fa >> 32);
    fy = (unsigned int)fa;
    (void)u; (void)v;

    if (DAT_80612e64 == 0) { FUN_80382708(); return; }

    savedMat   = (int)gx_set_sub_material_id(1);
    savedBlend = gx_set_blend_mode(0);

    if (FLOAT_8061b098 < FLOAT_80612e54) {
        float ring = FLOAT_8061b0b4 * i2f(x);   // ring intensity from timebase
        int s;
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        gx_set_active_material(-6);
        gx_draw_sprite_rect(u, v, u + (unsigned int)w, v + (unsigned int)h,
                            fx << 4, fy << 4, (fx + (unsigned int)w) * 0x10, (fy + (unsigned int)h) * 0x10);
        // 8 radial spokes.
        for (s = 0; s < 8; s = s + 1) {
            gx_draw_sprite_strip4(0ull,
                (double)i2f(fx), (double)i2f(fy), (double)ring, (double)i2f(fx),
                (double)i2f(fy), (double)ring, (double)ring,
                fx, fy, fx + (unsigned int)w, fy + (unsigned int)h, fx, fy,
                ring, 0x7f808080, 0x7f808080, 0x7f808080, 0x7f808080);
        }
    }

    if ((flags & 0xff) != 0) {
        gx_set_active_material(-6);
        gx_draw_sprite_rect(fx, fy, fx + (unsigned int)w, fy + (unsigned int)h,
                            fx << 4, fy << 4, (fx + (unsigned int)w) * 0x10, (fy + (unsigned int)h) * 0x10);
    }

    // Corner sprites + centre marker (state dependent).
    {
        int tex = (int)sprite_get_texture_id(0x123);
        int desc = *DAT_80610a20 + tex * 0x58;
        unsigned int uw = (unsigned int)*(unsigned short*)(desc + 0x4e) << 4;
        unsigned int vh = (unsigned int)*(unsigned short*)(desc + 0x52) << 4;
        gx_set_active_material(tex);
        gx_draw_sprite_rect(fx, fy, fx + (unsigned int)w, fy + (unsigned int)h, 0, 0, uw, vh);
    }

    if ((DAT_80612e64 == 2) || (DAT_80612e64 == 3)) {
        int tex = (int)sprite_get_texture_id(0x296);
        int desc = *DAT_80610a20 + tex * 0x58;
        unsigned int uw = (unsigned int)*(unsigned short*)(desc + 0x4e) << 4;
        unsigned int vh = (unsigned int)*(unsigned short*)(desc + 0x52) << 4;
        unsigned int hx = fx + ((unsigned int)w >> 1);
        unsigned int hy = fy + ((unsigned int)h >> 1);
        gx_set_color_update_mode(0);
        gx_set_active_material(tex);
        gx_set_scissor_rect(0, 0, DAT_806109c4, DAT_806109c8);
        gx_set_sub_material_id(0xe);
        gx_draw_sprite_rect(fx, fy, hx, hy, 0, 0, uw, vh);
        gx_draw_sprite_rect(hx, fy, fx + (unsigned int)w, hy, uw, 0, 0, vh);
        gx_draw_sprite_rect(fx, hy, hx, fy + (unsigned int)h, 0, vh, uw, 0);
        gx_draw_sprite_rect(hx, hy, fx + (unsigned int)w, fy + (unsigned int)h, uw, vh, 0, 0);
    } else if (DAT_80612e64 == 1) {
        // Particle field pass.
        int tex = (int)sprite_get_texture_id(0x28f);
        int desc = *DAT_80610a20 + tex * 0x58;
        unsigned int uw = (unsigned int)*(unsigned short*)(desc + 0x4e) << 4;
        unsigned int vh = (unsigned int)*(unsigned short*)(desc + 0x52) << 4;
        int off = 0, i;
        float pw = i2f((unsigned int)w) / FLOAT_8061b0d8;
        float ph = i2f((unsigned int)h) / FLOAT_8061b0d8;
        gx_set_scissor_rect(fx, fy, fx + (unsigned int)w, fy + (unsigned int)h);
        gx_set_color_update_mode(0);
        gx_set_sub_material_id(0xe);
        gx_fill_rect_clipped(fx, fy, fx + (unsigned int)w, fy + (unsigned int)h, 0);
        gx_set_sub_material_id(0x18);
        gx_set_active_material(tex);
        for (i = 0; i < 400; i = i + 1) {
            if ((double)FLOAT_8061b098 <= (double)*(float*)(DAT_805e5180 + off + 0x18)) {
                float bx = i2f(fx) + i2f((unsigned int)w) * *(float*)(DAT_805e5180 + off + 0x00);
                float by = i2f(fy) + i2f((unsigned int)h) * *(float*)(DAT_805e5180 + off + 0x04);
                if (*(float*)(DAT_805e5180 + off + 0x04) < FLOAT_8061b080) {
                    gx_draw_sprite_rect((int)(bx - pw), (int)(by - ph),
                                        (unsigned int)(int)(bx + pw), (unsigned int)(int)(by + ph),
                                        0, 0, uw, vh);
                }
            }
            off = off + 0x20;
        }
    }

    gx_set_sub_material_id((unsigned int)savedMat);
    gx_set_blend_mode(savedBlend);
    gx_set_color_update_mode(0);
    FUN_80382708();
}

// -------------------------------------------------------------------------
// 0x80360578  warpfx_draw_portal_mesh - 3D reflected portal mesh (GX FIFO).
//
// Non-matching: builds a reflected view-proj matrix, streams a textured
// tri-strip through the GX FIFO write-gather port (DAT_cc008000), and renders
// billboarded sparks. Vertex math condensed; preserves the FIFO write pattern.
// -------------------------------------------------------------------------
void warpfx_draw_portal_mesh(int arg1, int arg2, float* camPos)
{
    int destMat;
    float p0[3], p1[3], p2[3], p3[3];
    float viewProj[16];
    float ring[6 * 4];
    int i, j, off;

    (void)arg1; (void)arg2;
    FUN_803826c8();
    if (DAT_80612e64 == 0) { FUN_80382714(); return; }

    destMat = *DAT_80612e3c;

    // Four portal corner points, transformed into view space.
    p0[0] = FLOAT_8061b0e0; p0[1] = FLOAT_8061b0e4; p0[2] = FLOAT_8061b098;
    mtx4_transform_point_alias(camPos, p0);
    p1[0] = FLOAT_8061b0e8; p1[1] = FLOAT_8061b0e4; p1[2] = FLOAT_8061b098;
    mtx4_transform_point_alias(camPos, p1);
    p2[0] = FLOAT_8061b0e0; p2[1] = FLOAT_8061b0ec; p2[2] = FLOAT_8061b098;
    mtx4_transform_point_alias(camPos, p2);
    p3[0] = FLOAT_8061b0e8; p3[1] = FLOAT_8061b0ec; p3[2] = FLOAT_8061b098;
    mtx4_transform_point_alias(camPos, p3);

    {
        int savedBlend = gx_set_blend_mode(2);
        int savedMat;
        mtx4_transpose(&DAT_805831a8, &DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        object_build_view_proj_matrix(viewProj);
        gx_set_reflect_matrix(viewProj);
        savedMat = (int)gx_set_sub_material_id(2);

        // Build 6 ring vertices from the LUT/offset tables.
        off = 0;
        for (i = 0; i < 6; i = i + 1) {
            ring[i * 4 + 0] = (float)((double)FLOAT_80612e5c *
                (double)(float)((double)(float)(FLOAT_8061b0f0 * (FLOAT_8061b0f4 + *(float*)((int)&DAT_804876b0 + off))) - (double)*camPos) + (double)*camPos);
            ring[i * 4 + 1] = (float)((double)FLOAT_80612e5c * (double)(float)(FLOAT_8061b0f8 - camPos[1]) + (double)camPos[1]);
            ring[i * 4 + 2] = FLOAT_8061b098;
            mtx4_transform_point_alias(camPos, &ring[i * 4]);
            off = off + 4;
        }

        // Stream two textured tri-strips through the GX FIFO.
        for (i = 0; i < 2; i = i + 1) {
            int tex = (i == 0) ? (int)sprite_get_texture_id(0) : destMat;
            unsigned int color = (i == 0) ? 0x7f616b0au : 0x7f7f5764u;
            int voff = 0;
            gx_begin_textured_batch(3, 0x92, 0, 0xf, tex);
            for (j = 0; j < 5; j = j + 1) {
                DAT_cc008000 = *camPos;
                DAT_cc008000 = camPos[1];
                DAT_cc008000 = camPos[2];
                DAT_cc008000 = (float)color;
                DAT_cc008000 = *(float*)((int)&DAT_804876b0 + voff);
                DAT_cc008000 = ring[j * 4 + 0];
                DAT_cc008000 = ring[j * 4 + 1];
                DAT_cc008000 = ring[j * 4 + 2];
                DAT_cc008000 = (float)color;
                DAT_cc008000 = *(float*)((int)&DAT_804876b0 + voff);
                voff = voff + 4;
            }
            gx_end_batch();
        }
        gx_set_sub_material_id(2);
        gx_draw_quad_ptr4_mat(0ull, (double)FLOAT_8061b098, (double)FLOAT_8061b080,
                              (double)FLOAT_8061b080, (unsigned int)destMat, 0,
                              (int*)p0, (int*)p1, (int*)p2);

        // Billboarded sparks.
        {
            int tex = (int)sprite_get_texture_id(0x11d);
            gx_set_active_material(tex);
            for (i = 0; i < 3; i = i + 1) {
                float a = -(float)((double)FLOAT_8061b07c * (double)*(float*)((int)&DAT_80487684 + i * 0x10) - (double)FLOAT_8061b07c);
                float sc[3];
                (void)game_frand();
                sc[0] = p0[0] + (float)((double)(p1[0] - p0[0]) * (double)a);
                sc[1] = p0[1] + (float)((double)(p1[1] - p0[1]) * (double)a);
                sc[2] = FLOAT_8061b098;
                gx_draw_quad_ptr4_mat(0ull, (double)FLOAT_8061b098, (double)FLOAT_8061b080,
                                      (double)FLOAT_8061b080, (unsigned int)tex, 0,
                                      (int*)sc, (int*)p1, (int*)p2);
            }
        }

        // Restore camera reflect matrix + state.
        mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), &DAT_8058e3cc);
        DAT_80612250 = 1;
        DAT_80612258 = 0;
        gx_set_reflect_matrix(*(float**)(DAT_80611d54 + 0x204));
        gx_set_sub_material_id((unsigned int)savedMat);
        gx_set_blend_mode(savedBlend);
    }
    FUN_80382714();
}

// ---- portal state get/set (batch_1021) ----
extern float FLOAT_80612e58;             // portal aux timer
extern unsigned char DAT_804875c0[];     // spectator mode descriptor

// -------------------------------------------------------------------------
// 0x80360f70  warpfx_get_state - return the current portal effect state.
// -------------------------------------------------------------------------
int warpfx_get_state(void)
{
    return DAT_80612e64;
}

// -------------------------------------------------------------------------
// 0x80360f78  warpfx_set_state - set portal state and reset its timers.
// -------------------------------------------------------------------------
void warpfx_set_state(int state)
{
    FLOAT_80612e60 = FLOAT_8061b098;
    DAT_80612e64 = state;

    if (state == 2) {
        FLOAT_80612e5c = FLOAT_8061b080;
        return;
    }
    if (state >= 2) return;

    if (state == 0) {
        FLOAT_80612e5c = FLOAT_8061b098;
        FLOAT_80612e58 = FLOAT_8061b098;
        FLOAT_80612e54 = FLOAT_8061b098;
        FLOAT_80612e48 = FLOAT_8061b098;
        FLOAT_80612e44 = FLOAT_8061b098;
        if (DAT_80612e38 == 1) {
            screenfx_clear_slot(*(int*)(DAT_80611d74 + 4));
        } else if (DAT_80612e38 == 2) {
            spectator_instant_show();
        }
        if (DAT_80612e3c != 0) *DAT_80612e3c = -1;
    } else if (state >= 0) {   // state == 1 (open)
        *DAT_80612e3c = -1;
        warpfx_spawn_particles();
        if (DAT_80612e38 == 1) {
            screenfx_begin(*(int*)(DAT_80611d74 + 4), 1, 1, 0, 0, 0x80, 0x80);
            *DAT_80612e3c = (int)screenfx_ensure_scratch(*(int*)(DAT_80611d74 + 4));
        } else if (DAT_80612e38 == 2) {
            *DAT_80612e3c = spectator_get_render_callback();
            spectator_set_mode((double)FLOAT_8061b098, (int)&DAT_804875c0, 0);
            spectator_set_user_data((int)&warpfx_draw_portal_default);
        }
    }
}
