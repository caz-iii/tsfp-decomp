// boss3_surface.cpp - the "energy surface" simulation, top-level update/render
// and hit handling for the third segmented boss (control block DAT_80612a48,
// see boss3_tentacle.cpp for the tentacle links and block layout).
//
// The boss owns a 32x32 grid of 3-float "surface" vertices that behaves like a
// tensioned membrane / energy field: it is seeded flat, disturbed by impacts,
// integrated each frame with gravity + neighbour-averaging (wave) + damping, and
// drawn as a coloured triangle-strip mesh straight into the GX FIFO.
//
// DAT_80612a48 surface sub-layout (byte offsets from the block base):
//   +0x1410..+0x141c   tracked target point (world)     +0x1428..+0x1430 target-in
//   +0x1434            surface intensity (0..1 fade)     +0x1438 target-track timer
//   +0x143c  (0x180*row + 0xc*col)  velocity/height grid (float, 32x32)
//   +0x443c  (0x80 *row + 0x4 *col) height snapshot grid (float, 32x32)
//   +0x543c  (0x80 *row + 0x4 *col) per-vertex colour grid (u32, 32x32)
//   +0x574..+0x57c    surface centre world position
//   +0x643c           target-track FSM state (0 snap, 1 seek, 2 retract)
//   +0x649c           surface orientation matrix (row0 normal .649c/.64ac/.64bc,
//                     .64a0/.64a4 hold the packed normal used by the projector)
//
// Functions:
//   boss3_surface_grid_init      (0x8029dc88)  [game]
//   boss3_surface_apply_impulse  (0x8029ddcc)  [game]
//   boss3_surface_simulate       (0x8029def4)  [game]
//   boss3_surface_render         (0x8029e148)  [graphics - GX FIFO]
//   boss3_reset                  (0x8029e2bc)  [game]
//   boss3_update                 (0x8029e3e4)  [game]
//   boss3_surface_project_point  (0x8029e6c8)  [game]
//   boss3_surface_receive_hit    (0x8029e770)  [game]
//   boss3_tentacle_try_grab      (0x8029e8f4)  [game]
//   boss3_link_render            (0x8029e9a0)  [graphics]
//
// Address range: 0x8029dc88 - 0x8029eb57
// Subsystem: game (boss AI) + graphics (surface/link render)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int DAT_80612a48;              // boss3 control block base (byte-addressed)
extern int DAT_80611d54;              // active view/camera state (+0x208 view mtx*)
extern int DAT_80612250;              // GX matrix-dirty flag
extern int DAT_80612258;              // GX texgen/lighting flag
extern int DAT_806109c0;              // frame-delta ticks (xor bias)
extern volatile unsigned int DAT_cc008000;  // GX write-gather FIFO
extern int DAT_8058e3cc;              // scratch modelview matrix slot

// --- float / double constants ----------------------------------------------
extern const float FLOAT_80619568, FLOAT_80619570, FLOAT_80619574, FLOAT_80619580;
extern const float FLOAT_806195a4, FLOAT_806195a8, FLOAT_806195c0, FLOAT_806195ec;
extern const float FLOAT_8061956c, FLOAT_80619560, FLOAT_806196e4, FLOAT_806197dc;
extern const float FLOAT_8061963c, FLOAT_80619758, FLOAT_80619824, FLOAT_80619830;
extern const float FLOAT_80619834, FLOAT_80619838, FLOAT_8061983c, FLOAT_80619840;
extern const float FLOAT_80619844, FLOAT_80619848, FLOAT_806108e8, FLOAT_8061964c;
extern const double DOUBLE_80619630;  // frame-delta bias

// --- raw helpers (still FUN_ in the IDB) -----------------------------------
extern void FUN_803826cc(void);       // profiler push (surface simulate)
extern void FUN_80382718(void);       // profiler pop
extern void FUN_803826f0(void);       // profiler push (reset/update)
extern void FUN_8038273c(void);       // profiler pop
extern void FUN_802c6970(double a, double b, unsigned int tex, unsigned int col,
                         float* org, int* link);  // GX ribbon/beam renderer

// --- named cross-module helpers (call-site typed) --------------------------
extern double math_sqrt(double x);
extern unsigned int color_lerp(double t, unsigned int a, unsigned int b);
extern void mtx4_inverse_transform_point(float* mtx, float* v);
extern float* mtx4_pool_alloc(void);
extern void mtx4_multiply(float* a, float* b, float* out);
extern void mtx4_transpose(float* src, float* dst);
extern void gx_begin_textured_batch(int vtxfmt, int prim, int a, int count, int b);
extern void gx_end_batch(void);
extern void gx_set_sub_material_id(int id);
extern unsigned int sprite_get_texture_id(int slot);
extern unsigned int player_ai_get_active_index(void);
extern void particle_slot_spawn(int type, float* pos, float* vel);
extern void particle_cached_set_position(int* handle, float* pos);
extern void particle_cached_set_drag(double drag, int* handle);
extern void spark_spawn_fading(void);
extern void spark_set_last_color(int rgb);

} // extern "C"

// same-module forward decls
static void boss3_surface_grid_init(void);
static void boss3_surface_apply_impulse(float* pt);
static void boss3_surface_simulate(void);
static bool boss3_surface_project_point(float* in, float* out);

// boss3 links live at +0xf30, each 0x1a0 bytes (see boss3_tentacle.cpp).
// These two are defined with C++ linkage in boss3_tentacle.cpp.
void boss3_link_init(double phaseB, double phaseA, int rec);
void boss3_tentacle_tick(float* rec);

// convenient byte-addressed accessors into the control block
static inline float* SF(int off)  { return (float*)(DAT_80612a48 + off); }
static inline int*   SI(int off)  { return (int*)(DAT_80612a48 + off); }

// ===========================================================================
// 0x8029dc88  boss3_surface_grid_init
// Seed the 32x32 surface grid: velocity grid <- flat height, colour grid <- 0,
// snapshot grid position <- (x0, row*step, col*step). Then clamp each vertex's
// yz displacement magnitude to a maximum radius.
// ===========================================================================
static void boss3_surface_grid_init(void) {
    float step   = FLOAT_80619830;   // grid spacing
    float x0     = FLOAT_80619824;   // base x
    float flat   = FLOAT_80619568;   // 0.0 seed
    float origin = FLOAT_80619570;   // first row/col coord

    int velBase  = DAT_80612a48 + 0x143c;
    int posBase  = DAT_80612a48 + 0x443c;
    int colBase  = DAT_80612a48 + 0x543c;

    int velRow = 0, posRow = 0;
    float rowCoord = origin;
    for (int r = 0; r < 0x20; r++) {
        int velCol = 0, posCol = 0;
        float colCoord = origin;
        for (int c = 0; c < 0x20; c++) {
            *(float*)(posBase + velRow + velCol) = flat;
            float* p = (float*)(velBase + posRow + posCol);
            posCol += 0xc;
            *(int*)(colBase + velRow + velCol) = 0;
            velCol += 4;
            p[0] = x0;
            p[1] = rowCoord;
            p[2] = colCoord;
            colCoord += step;
        }
        rowCoord += step;
        posRow += 0x180;
        velRow += 0x80;
    }

    double maxSq = (double)FLOAT_80619580;   // max radius squared
    posRow = 0;
    for (int r = 0; r < 0x20; r++) {
        int posCol = 0;
        for (int c = 0; c < 0x20; c++) {
            int p = velBase + posRow + posCol;
            double d = (double)(*(float*)(p + 8) * *(float*)(p + 8) +
                                *(float*)(p + 4) * *(float*)(p + 4));
            if (maxSq < d) {
                double len = math_sqrt(d);
                float scale = (float)(maxSq / len);
                *(float*)(p + 8) = *(float*)(p + 8) * scale;
                *(float*)(p + 4) = *(float*)(p + 4) * scale;
            }
            posCol += 0xc;
        }
        posRow += 0x180;
    }
}

// ===========================================================================
// 0x8029ddcc  boss3_surface_apply_impulse
// Convert a world point into surface-local space, map it to a grid cell and add
// a vertical impulse to the 3x3 neighbourhood around it (edge cells skipped).
// ===========================================================================
static void boss3_surface_apply_impulse(float* pt) {
    float local[3];
    local[0] = pt[0];
    local[1] = pt[1];
    local[2] = pt[2];
    mtx4_inverse_transform_point(SF(0x649c), local);

    int cx = (int)(FLOAT_80619834 * (FLOAT_80619580 + local[1]));
    int cy = (int)(FLOAT_80619834 * (FLOAT_80619580 + local[2]));
    if (!((-1 < cx + 1) && (cx + 1 < 0x20) && (-1 < cy + 1) && (cy + 1 < 0x20)))
        return;

    float amt = FLOAT_806195a4;
    int r    = cx - 1;
    int rowB = r * 0x180;
    int c0   = cy - 1;
    for (; r < cx + 3; r++) {
        if ((0 < r) && (r < 0x1f)) {
            int colOff = c0 * 0xc;
            for (int c = c0; c < cy + 3; c++) {
                if ((0 < c) && (c < 0x1f)) {
                    int slot = DAT_80612a48 + colOff + 0x143c;
                    *(float*)(rowB + slot) = *(float*)(rowB + slot) + amt;
                }
                colOff += 0xc;
            }
        }
        rowB += 0x180;
    }
}

// ===========================================================================
// 0x8029def4  boss3_surface_simulate
// One integration step of the surface: (1) apply gravity/decay to every vertex
// and swap velocity<->snapshot, (2) neighbour-average interior vertices to
// propagate waves with damping, (3) compute a per-vertex colour and settle the
// height back toward rest.
// ===========================================================================
static void boss3_surface_simulate(void) {
    FUN_803826cc();

    float gravity = FLOAT_8061963c;
    int velBase = DAT_80612a48 + 0x143c;
    int snapBase = DAT_80612a48 + 0x443c;
    float bias = FLOAT_80619838 * *(float*)(DAT_80612a48 + 0x1434);
    int colBase = DAT_80612a48 + 0x543c;

    // pass 1: gravity + velocity/snapshot swap
    int velRow = 0, snapRow = 0;
    float wave = FLOAT_80619758;
    float damp = FLOAT_8061983c;
    for (int r = 0; r < 0x20; r++) {
        int vp = velBase + velRow;
        int sp = snapBase + snapRow;
        int velCol = 0, snapCol = 0;
        for (int c = 0; c < 0x20; c++) {
            *(float*)(vp + velCol) = *(float*)(vp + velCol) + gravity;
            float prev = *(float*)(sp + snapCol);
            *(int*)(sp + snapCol) = *(int*)(vp + velCol);
            snapCol += 4;
            *(float*)(vp + velCol) = -prev;
            velCol += 0xc;
        }
        velRow += 0x180;
        snapRow += 0x80;
    }

    // pass 2: neighbour-average interior vertices (wave propagation + damping)
    velRow = 0x180;
    snapRow = 0x80;
    for (int r = 1; r < 0x1f; r++) {
        int vp = velBase + velRow;
        int snapCol = 4, velCol = 0xc;
        for (int c = 0; c < 0x1e; c++) {
            float* n = (float*)(snapBase + snapRow + snapCol);
            snapCol += 4;
            *(float*)(vp + velCol) =
                *(float*)(vp + velCol) +
                wave * (n[1] + n[-1] + n[-0x20] + n[0x20]) + n[0];
            *(float*)(vp + velCol) =
                -(damp * (*(float*)(vp + velCol) - n[0]) - *(float*)(vp + velCol));
            velCol += 0xc;
        }
        velRow += 0x180;
        snapRow += 0x80;
    }

    // pass 3: colour + settle back toward rest
    double gain  = (double)FLOAT_806197dc;
    double clamp = (double)FLOAT_80619580;
    double cscale = (double)FLOAT_80619840;
    double decay = (double)FLOAT_8061963c;
    velRow = 0;
    int colRow = 0;
    for (int r = 0; r < 0x20; r++) {
        int vp = velBase + velRow;
        int cp = colBase + colRow;
        int velCol = 0, colCol = 0;
        for (int c = 0; c < 0x20; c++) {
            double h = (double)(float)(gain * (double)*(float*)(vp + velCol));
            double t = clamp;
            if ((h <= clamp) && (t = h, h < (double)FLOAT_80619568))
                t = (double)FLOAT_80619568;
            *(unsigned int*)(cp + colCol) =
                ((unsigned int)((int)(cscale * t) + (int)bias) << 24) | 0xf3205u;
            unsigned int col = color_lerp(t, 0x7f0f6060u, *(unsigned int*)(cp + colCol));
            *(unsigned int*)(cp + colCol) = col;
            colCol += 4;
            *(float*)(vp + velCol) = (float)((double)*(float*)(vp + velCol) - decay);
            velCol += 0xc;
        }
        velRow += 0x180;
        colRow += 0x80;
    }

    FUN_80382718();
}

// ===========================================================================
// 0x8029e148  boss3_surface_render   [graphics - GX FIFO]
// Build a modelview matrix at the surface centre, upload it, then stream 31
// triangle-strips (32 verts each) of position+colour straight into the GX FIFO
// (DAT_cc008000). Colours are byte-swapped to big-endian GX RGBA order.
// ===========================================================================
void boss3_surface_render(void) {
    float* mv = mtx4_pool_alloc();
    float* wm = mtx4_pool_alloc();
    int velBase = DAT_80612a48 + 0x143c;
    int colBase = DAT_80612a48 + 0x543c;

    mv[0xc] = *(float*)(DAT_80612a48 + 0x574);
    mv[0xd] = *(float*)(DAT_80612a48 + 0x578);
    mv[0xe] = *(float*)(DAT_80612a48 + 0x57c);
    mtx4_multiply(mv, *(float**)(DAT_80611d54 + 0x208), wm);
    mtx4_transpose(wm, (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;

    int velRow = 0, colRow = 0;
    for (int r = 0; r < 0x1f; r++) {
        gx_begin_textured_batch(2, 0x81, 0, 0x40, -1);
        int velCol = 0, colCol = 0;
        for (int c = 0; c < 0x20; c++) {
            float* p = (float*)(velBase + velRow + velCol);
            unsigned int* cc = (unsigned int*)(colBase + colRow + colCol);
            DAT_cc008000 = *(unsigned int*)&p[0];
            DAT_cc008000 = *(unsigned int*)&p[1];
            DAT_cc008000 = *(unsigned int*)&p[2];
            unsigned int col = cc[0];
            DAT_cc008000 = col << 24 | (col & 0xff00) << 8 | col >> 8 & 0xff00 | col >> 24;
            DAT_cc008000 = *(unsigned int*)&p[0x60];
            DAT_cc008000 = *(unsigned int*)&p[0x61];
            DAT_cc008000 = *(unsigned int*)&p[0x62];
            col = cc[0x20];
            DAT_cc008000 = col << 24 | (col & 0xff00) << 8 | col >> 8 & 0xff00 | col >> 24;
            colCol += 4;
            velCol += 0xc;
        }
        gx_end_batch();
        velRow += 0x180;
        colRow += 0x80;
    }

    mtx4_transpose(*(float**)(DAT_80611d54 + 0x208), (float*)&DAT_8058e3cc);
    DAT_80612250 = 1;
    DAT_80612258 = 0;
}

// ===========================================================================
// 0x8029e2bc  boss3_reset
// Reset the whole boss: init the 3 tentacle links spaced along an arc, rebuild
// the surface grid, then clear the tracked-target and intensity state.
// ===========================================================================
void boss3_reset(void) {
    FUN_803826f0();

    double sx   = (double)FLOAT_806195c0;
    double bx   = (double)FLOAT_806195ec;
    double sz   = (double)FLOAT_806196e4;
    double bias = DOUBLE_80619630;
    int linkOff = 0;
    for (unsigned int i = 0; i < 3; i++) {
        double t = (double)(float)((double)CONCAT44(0x43300000, i ^ 0x80000000) - bias);
        boss3_link_init((double)(float)(sx * t + bx),
                        (double)(float)(sz * t),
                        DAT_80612a48 + linkOff + 0xf30);
        linkOff += 0x1a0;
    }

    boss3_surface_grid_init();

    float z = FLOAT_80619568;
    *SF(0x1410) = z; *SF(0x1414) = z; *SF(0x1418) = z;
    *SF(0x1428) = z; *SF(0x142c) = z; *SF(0x1430) = z;
    *SF(0x1434) = z;
    *SI(0x643c) = 0;
    *SF(0x1438) = z;

    FUN_8038273c();
}

// ===========================================================================
// 0x8029e3e4  boss3_update
// Per-frame boss update: derive the surface centre from its orientation, tick
// the 3 tentacles, run the target-track FSM (snap / seek / retract at +0x643c),
// spark the grabbing links, ease the surface intensity, then simulate the grid.
// ===========================================================================
void boss3_update(void) {
    FUN_803826f0();

    float s = FLOAT_80619824;
    *SF(0x1428) = s * *SF(0x649c) + *SF(0x574);
    *SF(0x142c) = s * *SF(0x64ac) + *SF(0x578);
    *SF(0x1430) = s * *SF(0x64bc) + *SF(0x57c);

    int linkOff = 0;
    for (int i = 0; i < 3; i++) {
        boss3_tentacle_tick((float*)(DAT_80612a48 + linkOff + 0xf30));
        linkOff += 0x1a0;
    }
    float lerp = FLOAT_806195a8;
    float rest = FLOAT_80619568;

    int state = *SI(0x643c);
    if (state == 0) {
        *SI(0x1410) = *SI(0x1428);
        *SI(0x1414) = *SI(0x142c);
        *SI(0x1418) = *SI(0x1430);
    } else if (state == 1) {
        float dy = *SF(0x142c) - *SF(0x1414);
        float dx = *SF(0x1428) - *SF(0x1410);
        float dz = *SF(0x1430) - *SF(0x1418);
        if (FLOAT_80619844 <= dz * dz + dx * dx + dy * dy) {
            *SF(0x1410) = lerp * dx + *SF(0x1410);
            *SF(0x1414) = lerp * dy + *SF(0x1414);
            *SF(0x1418) = lerp * dz + *SF(0x1418);
        } else {
            *SF(0x1410) = *SF(0x1428);
            *SI(0x1414) = *SI(0x142c);
            *SI(0x1418) = *SI(0x1430);
            *SI(0x643c) = 0;
        }
    } else {
        double bias = DOUBLE_80619630;
        float tt = (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - bias);
        *SF(0x1438) = -(FLOAT_806108e8 * (FLOAT_80619580 / tt) - *SF(0x1438));
        *SF(0x1410) = *SF(0x141c) + *SF(0x9c);
        *SF(0x1414) = *SF(0x1420) + *SF(0xa0);
        *SF(0x1418) = *SF(0x1424) + *SF(0xa4);
        if (*SF(0x1438) < rest) {
            *SI(0x643c) = 1;
            *SF(0x1438) = rest;
        }
    }

    double intensity = (double)FLOAT_80619568;
    if (0 < *SI(0x58c)) {
        double sparkStep = (double)FLOAT_80619848;
        int off = 0;
        for (int i = 0; i < 3; i++) {
            if (*(int*)(DAT_80612a48 + off + 0xf5c) == 2) {
                spark_spawn_fading();
                spark_set_last_color(0xf3205);
                intensity = (double)(float)(intensity + sparkStep);
            }
            off += 0x1a0;
        }
    }

    // ease intensity (+0x1434) toward the spark-derived target
    double cur = (double)*SF(0x1434);
    if (intensity <= cur) {
        if ((intensity < cur) &&
            (*SF(0x1434) = (float)(cur - (double)FLOAT_80619560),
             (double)*SF(0x1434) < intensity)) {
            *SF(0x1434) = (float)intensity;
        }
    } else {
        *SF(0x1434) = (float)(cur + (double)FLOAT_80619560);
        if (intensity < (double)*SF(0x1434))
            *SF(0x1434) = (float)intensity;
    }

    boss3_surface_simulate();
    FUN_8038273c();
}

// ===========================================================================
// 0x8029e6c8  boss3_surface_project_point
// Project a world point onto the surface plane (centre +0x1428, normal +0x649c/
// +0x64a0/+0x64a4). Writes the projected point to 'out' and returns true when it
// lands within the surface radius.
// ===========================================================================
static bool boss3_surface_project_point(float* in, float* out) {
    float maxSq = FLOAT_80619580;
    float ny = *SF(0x64a0);
    float nz = *SF(0x64a4);
    float d = (in[2] - *SF(0x1430)) * nz +
              (in[0] - *SF(0x1428)) * *SF(0x649c) +
              (in[1] - *SF(0x142c)) * ny;
    out[0] = d * *SF(0x649c) + in[0];
    out[1] = d * ny + in[1];
    out[2] = d * nz + in[2];
    float dy = out[1] - *SF(0x142c);
    float dx = out[0] - *SF(0x1428);
    float dz = out[2] - *SF(0x1430);
    return dz * dz + dx * dx + dy * dy < maxSq;
}

// ===========================================================================
// 0x8029e770  boss3_surface_receive_hit
// Handle a projectile/impact at 'pt'. If the boss is in a vulnerable state, the
// surface is lit, and the point projects onto it: disturb the grid, latch the
// target-track FSM to "retract" (state 2) toward the hit and spawn an impact
// particle aimed away from the surface centre. Returns 1 if the hit registered.
// ===========================================================================
int boss3_surface_receive_hit(float* pt) {
    unsigned int who = player_ai_get_active_index();
    float proj[3];
    if (((1 < who) && (who != 8)) ||
        (*SF(0x1434) <= FLOAT_80619568) ||
        (!boss3_surface_project_point(pt, proj))) {
        return 0;
    }

    boss3_surface_apply_impulse(proj);
    float radius = FLOAT_80619580;
    *SF(0x1410) = proj[0];
    *SF(0x1414) = proj[1];
    *SF(0x1418) = proj[2];
    *SF(0x141c) = *SF(0x1410) - *SF(0x9c);
    *SF(0x1420) = *SF(0x1414) - *SF(0xa0);
    *SF(0x1424) = *SF(0x1418) - *SF(0xa4);
    *SI(0x643c) = 2;
    *SF(0x1438) = radius;

    float dir[3];
    dir[0] = pt[0] - *SF(0x9c);
    dir[1] = (pt[1] - *SF(0xa0)) * FLOAT_80619574;
    dir[2] = pt[2] - *SF(0xa4);
    double len = math_sqrt((double)(dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1]));
    float scale = (float)((double)FLOAT_80619580 / len);
    dir[0] *= scale;
    dir[1] *= scale;
    dir[2] *= scale;
    particle_slot_spawn(0, proj, dir);
    return 1;
}

// ===========================================================================
// 0x8029e8f4  boss3_tentacle_try_grab
// Find the nearest available tentacle link (state < 5) to target entity 'target'
// within a max distance and, if found, latch that link onto the target: set link
// state 5, store the target pointer (+0x48) and mark the target caught (+0x3c).
// ===========================================================================
void boss3_tentacle_try_grab(int target) {
    float rest = FLOAT_80619568;
    int best = -1;
    float bestDist = FLOAT_8061964c;
    int off = 0;
    for (int i = 0; i < 3; i++) {
        int link = DAT_80612a48 + off;
        if (*(int*)(link + 0xf5c) < 5) {
            float dy = *(float*)(target + 0x28) - *(float*)(link + 0xf40);
            float dx = *(float*)(target + 0x24) - *(float*)(link + 0xf3c);
            float dz = *(float*)(target + 0x2c) - *(float*)(link + 0xf44);
            float dsq = dz * dz + dx * dx + dy * dy;
            if (dsq < bestDist) {
                best = i;
                bestDist = dsq;
            }
        }
        off += 0x1a0;
    }
    if (best < 0)
        return;
    int link = DAT_80612a48 + best * 0x1a0 + 0xf30;
    *(int*)(link + 0x2c) = 5;
    *(float*)(link + 0x34) = rest;
    *(int*)(link + 0x48) = target;
    *(int*)(target + 0x3c) = 1;
}

// ===========================================================================
// 0x8029e9a0  boss3_link_render   [graphics]
// Render one tentacle link: extrapolate a trail point, update its cached trail
// particle (position + drag), set the sprite material, animate the link's colour
// set when in the "fade" state (3), and draw the textured beam via FUN_802c6970.
// ===========================================================================
void boss3_link_render(int link) {
    float trail[3];
    trail[0] = *(float*)(link + 0xc) + (*(float*)(link + 0xc) - *(float*)(link + 0x18));
    trail[1] = *(float*)(link + 0x10) + (*(float*)(link + 0x10) - *(float*)(link + 0x1c));
    trail[2] = *(float*)(link + 0x14) + (*(float*)(link + 0x14) - *(float*)(link + 0x20));

    int h = *(int*)(link + 0x4c);
    particle_cached_set_position(&h, trail);
    int h2 = *(int*)(link + 0x4c);
    particle_cached_set_drag((double)(FLOAT_80619580 - *(float*)(link + 0x38)), &h2);
    gx_set_sub_material_id(1);

    if (*(int*)(link + 0x2c) == 3) {
        double t = (double)(*(float*)(link + 0x34) / FLOAT_8061956c);
        *(unsigned int*)(link + 0x70) = color_lerp(t, 0x640000, 0x64000000);
        *(unsigned int*)(link + 0x7c) = color_lerp(t, 0, 0);
        *(unsigned int*)(link + 0x80) = color_lerp(t, 0xa281400, 0x28140a00);
        *(unsigned int*)(link + 0x74) = color_lerp(t, 0xa321400, 0x32140a00);
        unsigned int col = color_lerp(t, 0x7f000000, 0x7f050f32);
        unsigned int tex = sprite_get_texture_id(0);
        FUN_802c6970((double)FLOAT_806195a8, (double)FLOAT_80619580, tex, col,
                     (float*)(link + 0xc), (int*)(DAT_80612a48 + 0x5c4));
    } else {
        *(unsigned int*)(link + 0x70) = 0x640000;
        *(unsigned int*)(link + 0x7c) = 0;
        *(unsigned int*)(link + 0x80) = 0xa281400;
        *(unsigned int*)(link + 0x74) = 0xa321400;
        unsigned int tex = sprite_get_texture_id(0);
        FUN_802c6970((double)FLOAT_806195a8, (double)FLOAT_80619580, tex, 0x7f000000,
                     (float*)(link + 0xc), (int*)(DAT_80612a48 + 0x5c4));
    }
}
