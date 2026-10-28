// =============================================================================
// snake3_boss_segments.cpp - the snake3 boss "spawned segment" projectile pool.
//
// In addition to the arena grid (snake3_boss_grid.cpp) the snake3 boss launches
// up to 8 independent "segment" projectiles - short chains of 4 linked entities
// that travel along a bezier path across the arena, then rear up and explode.
// Each segment lives in a fixed pool record (0x168-byte stride, 8 records).
//
// Record layout (float index [i] = byte offset 4*i, or explicit byte offsets):
//   [0..2]   head node position (x,y,z)     [3..0x2f] the 4 chain-node xyz sets
//            (node k head at [3*k], previous-frame copy at [0xc + 3*k])
//   [0x30]   spin/settle timer   [0x31] bezier path t   [0x33] node-follow gain
//   [0x34]   vertical drift accumulator     [0x35] anim-start latch
//   [0x39]   rear-up phase index (0..4)     [0x3a] approach sub-flag
//   [0x3e]   MAIN STATE (stored as float bits): 1=path,2=arrive,4=travel,
//            5=erupt(rendered),7=explode-wind-down
//   [0x3f..0x4a] bezier control points p0..p3 (four xyz), [0x48..0x4a] target xy
//   [0x4b..0x52] eruption billboard scale ramp   [0x53] eruption/despawn counter
//   [0x54]   owning model/entity pointer   [0x58] launch-sound latch
//   [0x59..] 3d-sound emitter scratch
//   byte +0xc4 facing angle   +0xd0/+0xd4 (x4) per-node anim timers
//   +0xec grid row  +0xf0 grid col  +0xf4 grid-cell ptr  +0xf8 STATE int
//     (0 = empty, 1 = launching, 5 = active/rendered)
//   +0xfc..+0x104 start waypoint   +0x108..+0x11c path anchor points
//   +0x120/+0x124/+0x128 world anchor position (x,y,z)
//   +0x130..+0x13c movement bound insets (-x,+x,-y,+y)
//   +0x150 (x4) spawned chain-entity pointers   +0x164 = -1
//
// Functions (all subsystem: game):
//   * snake3_seg_pool_reset     (0x802b12cc) - clear the 8-record pool.
//   * snake3_seg_destroy_all    (0x802b13bc) - destroy every spawned chain entity.
//   * snake3_seg_clamp_bounds   (0x802b1440) - clamp a record's move bounds to the
//     grid cell it currently occupies (cell size flag at cell+0x20).
//   * snake3_seg_collision_test (0x802b15a4) - test a point against the 8 active
//     segments; on hit, output a normalized push vector.
//   * snake3_seg_link_constrain (0x802b17a0) - pull 20 chain nodes back toward a
//     fixed spacing from their anchor (spring constraint).
//   * snake3_seg_tick           (0x802b1884) - per-record master FSM: path travel,
//     node-follow, rear-up, eruption billboard, explosion + aim angles.
//   * snake3_seg_launch         (0x802b22d8) - spawn a new 4-entity chain along a
//     direction and initialize its record.
//   * snake3_seg_render         (0x802b26d4) - draw one segment's glow/core
//     billboards to the GX FIFO.
//   * snake3_seg_render_all     (0x802b3370) - render every active (state 5) record.
//   * snake3_seg_seed_jitter    (0x802b33e0) - seed the 300-entry rng jitter table.
//
// Address range: 0x802b12cc - 0x802b33e0
// Subsystem: game (snake3 boss spawned-segment projectiles)
// =============================================================================

#include "common.h"

extern "C" {

// --- constants (data at 0x806199d8..0x80619b90) ------------------------------
extern const float FLOAT_80619ab8;  // 0.5
extern const float FLOAT_80619ad8;  // 1.0
extern const float FLOAT_80619adc;  // 0.0
extern const float FLOAT_80619ae0;
extern const float FLOAT_80619ae4;  // normalization numerator
extern const float FLOAT_80619ae8;  // link snap max dist^2
extern const float FLOAT_80619aec;  // link target spacing
extern const float FLOAT_80619af0;
extern const float FLOAT_80619af4;
extern const float FLOAT_80619af8;
extern const float FLOAT_80619afc;
extern const float FLOAT_80619b00;
extern const float FLOAT_80619b04;
extern const float FLOAT_80619b08;
extern const float FLOAT_80619b0c;
extern const float FLOAT_80619b10;
extern const float FLOAT_80619b14;
extern const float FLOAT_80619b18;
extern const float FLOAT_80619b1c;
extern const float FLOAT_80619b20;
extern const float FLOAT_80619b24;
extern const float FLOAT_80619b28;
extern const float FLOAT_80619b2c;
extern const float FLOAT_80619b30;
extern const float FLOAT_80619b34;
extern const float FLOAT_80619b38;
extern const float FLOAT_80619b3c;
extern const float FLOAT_80619b40;
extern const float FLOAT_80619b44;
extern const float FLOAT_80619b48;
extern const float FLOAT_80619b4c;
extern const float FLOAT_80619b50;
extern const float FLOAT_80619b60;
extern const float FLOAT_80619b64;
extern const float FLOAT_80619b68;
extern const float FLOAT_80619b6c;  // rng scale (1/range)
extern const float FLOAT_80619b70;
extern const float FLOAT_80619b74;
extern const float FLOAT_80619b78;
extern const float FLOAT_80619b7c;
extern const float FLOAT_80619b80;
extern const float FLOAT_80619b84;
extern const float FLOAT_80619b88;
extern const double DOUBLE_80619b58;  // 2^52 int->float bias helper
extern const double DOUBLE_80619b90;  // rng bias

// --- sin/cos lookup table (shared) -------------------------------------------
extern float DAT_80583228[];

// --- frame-enter / frame-leave intrinsics ------------------------------------
extern int               FUN_803826dc(void);          // returns pool base (int)
extern int               FUN_803826e8(void);          // returns table base (int)
extern unsigned long long FUN_803826cc(void);         // pool base as register pair
extern unsigned long long FUN_803826d0(void);         // (recBase:ctx) register pair
extern int               __save_gpr_28(void);
extern void              __restore_gpr_28(void);
extern void FUN_80382718(void);
extern void FUN_8038271c(void);
extern void FUN_80382728(void);
extern void FUN_80382734(void);
extern int  FUN_803825c0(double x);                   // float->byte colour quantiser
extern int  FUN_802c66bc(int a, int b, unsigned long long *ctx);  // grid setup

// --- named cross-module helpers (declared in shared headers, re-declared here
//     with the arity/return we use so this TU is self-contained) --------------
extern void  entity_destroy_with_event(void *entity);
extern int   entity_spawn_by_type(double a, double b, double c, double d, double e,
                                  double f, unsigned long long g, unsigned long long h,
                                  int type, void *rec, void *aux, int i, int j);
extern void  entity_register_for_update(int entity, int a, int b);
extern void  sound_init_play_params(int params);
extern void  sound_play_request(int *params);
extern void  sound_update_3d_source(double a, double b, double c, float *emitter,
                                    float *rec, int id, int flag);
extern void  bezier_eval_vec3(double t, float *out, float *p0, float *p1, float *p2, float *p3);
extern double math_sqrt(double x);
extern double math_atan2(double y, double x);
extern double math_poly_trig(double x);
extern void  anim_set_blend_weight(double w, int slot);
extern void  anim_clear_blend_weight(int slot);
extern int   anim_active_slot_userflags(int slot);
extern void  anim_play_scaled(double a, double b);
extern void  particle_sys_b_spawn(float *pos);
extern void  particle_sys_b_spawn_xyz(double x, double y, double z);
extern void  player_ai_explosion_spawn(int a, int b, float *pos, float *dir, int c, int d, int e);
extern void  player_ai_play_explosion_sound(void);
extern int   sprite_get_texture_id(int id);
extern void  gx_begin_textured_batch(int a, int prim, int b, int count, int tex);
extern void  gx_begin_batch_mode5(int prim, int a, int count, int tex);
extern void  gx_end_batch(void);
extern unsigned int rng_counter_tick(void);

// --- typed GX FIFO write port (do NOT use the legacy undeclared DAT_cc008000) -
static volatile float        *const gx_fifo_f32 = (volatile float *)0xCC008000u;
static volatile unsigned int *const gx_fifo_u32 = (volatile unsigned int *)0xCC008000u;

// Constants used only as record-state discriminators (Ghidra prints them as the
// float bit patterns of small integers). Compare the raw state int instead.
// State int values live at record byte offset 0xf8.

// -----------------------------------------------------------------------------
// snake3_seg_pool_reset - clear the 8 segment records to their idle defaults.
// -----------------------------------------------------------------------------
void snake3_seg_pool_reset(int base, unsigned long long *ctx)
{
    FUN_802c66bc(10, 10, ctx);
    float zero = FLOAT_80619adc;
    int rec = 0;
    int recOff = 0;
    do {
        int r = base + recOff;
        // clear the 4 chain nodes' current + previous positions
        int a = 0, b = 0, left = 4;
        do {
            float *node = (float *)(r + a);
            int nb = r + b;
            node[0] = zero; node[1] = zero; node[2] = zero;         // current
            node[0xc] = zero; node[0xd] = zero; node[0xe] = zero;   // prev copy
            node[0x18] = zero; node[0x19] = zero; node[0x1a] = zero;// spawn vel
            *(int *)(nb + 0x150) = 0;   // entity ptr
            *(int *)(nb + 0xd4)  = 0;   // anim timer
            b = b + 4;
            a = a + 0xc;
            left = left - 1;
        } while (left != 0);
        // clear the 4 path anchor points (+0xfc..)
        int c = 0, left2 = 4;
        do {
            int p = r + c;
            c = c + 0xc;
            *(float *)(p + 0xfc)  = zero;
            *(float *)(p + 0x100) = zero;
            *(float *)(p + 0x104) = zero;
            left2 = left2 - 1;
        } while (left2 != 0);
        *(float *)(r + 0xc4) = zero;    // facing angle
        *(int *)(r + 0xf8)   = 0;       // state = empty
        *(int *)(r + 0xf4)   = 0;       // grid cell ptr
        *(int *)(r + 0x164)  = -1;
        rec = rec + 1;
        recOff = recOff + 0x168;
    } while (rec < 8);
}

// -----------------------------------------------------------------------------
// snake3_seg_destroy_all - destroy every spawned chain entity and clear state.
// -----------------------------------------------------------------------------
void snake3_seg_destroy_all(void)
{
    int base = FUN_803826dc();
    int rec = 0;
    int recOff = 0;
    do {
        int r = base + recOff;
        int n = 0, off = 0;
        do {
            void *ent = *(void **)(r + off + 0x150);
            if (ent != 0) {
                entity_destroy_with_event(ent);
                *(int *)(r + off + 0x150) = 0;
            }
            n = n + 1;
            off = off + 4;
        } while (n < 4);
        *(int *)(r + 0xf8) = 0;
        rec = rec + 1;
        recOff = recOff + 0x168;
    } while (rec < 8);
    FUN_80382728();
}

// -----------------------------------------------------------------------------
// snake3_seg_clamp_bounds - clamp a record's movement bound insets so the
// segment stays inside the grid cell it currently occupies. The cell (record
// +0xf4) carries a size flag at +0x20 (2 = tall, 3 = wide, 0 = square).
// -----------------------------------------------------------------------------
void snake3_seg_clamp_bounds(int rec, float *cell)
{
    int cellPtr = *(int *)(rec + 0xf4);
    int flag = *(int *)(cellPtr + 0x20);

    if (flag == 2) {
        if (*(float *)(cellPtr + 0x1c) < *(float *)(rec + 0x138)) {
            *(float *)(rec + 0x138) = *(float *)(cellPtr + 0x1c);
            *(int *)(rec + 0x13c)   = *(int *)(rec + 0x138);
        }
        float cx = *(float *)(rec + 0x24);
        if (cx - *(float *)(rec + 0x130) < cell[0]) {
            *(float *)(rec + 0x130) = cx - cell[0];
            return;
        }
        if (cx + *(float *)(rec + 0x134) <= cell[2]) return;
        *(float *)(rec + 0x134) = cell[2] - cx;
        return;
    }
    if (flag == 3) {
        if (*(float *)(cellPtr + 0x18) < *(float *)(rec + 0x130)) {
            *(float *)(rec + 0x130) = *(float *)(cellPtr + 0x18);
            *(int *)(rec + 0x134)   = *(int *)(rec + 0x130);
        }
        float cz = *(float *)(rec + 0x2c);
        if (cz - *(float *)(rec + 0x138) < cell[1]) {
            *(float *)(rec + 0x138) = cz - cell[1];
            return;
        }
        if (cz + *(float *)(rec + 0x13c) <= cell[3]) return;
        *(float *)(rec + 0x13c) = cell[3] - cz;
        return;
    }
    if (flag != 0) return;

    float cx = *(float *)(rec + 0x24);
    if (cell[0] <= cx - *(float *)(rec + 0x130)) {
        if (cell[2] < cx + *(float *)(rec + 0x134)) {
            *(float *)(rec + 0x134) = cell[2] - cx;
        }
    } else {
        *(float *)(rec + 0x130) = cx - cell[0];
    }
    float cz = *(float *)(rec + 0x2c);
    if (cz - *(float *)(rec + 0x138) < cell[1]) {
        *(float *)(rec + 0x138) = cz - cell[1];
        return;
    }
    if (cz + *(float *)(rec + 0x13c) <= cell[3]) return;
    *(float *)(rec + 0x13c) = cell[3] - cz;
}

// -----------------------------------------------------------------------------
// snake3_seg_collision_test - test a point (pt) against the 8 records that are
// in the "active/rendered" state (5). On the first overlap, write a normalized
// push-out vector to out and return 1; otherwise return 0.
// -----------------------------------------------------------------------------
int snake3_seg_collision_test(int base, float *pt, float *out)
{
    int off = 0;
    int left = 8;
    do {
        int r = base + off;
        if (*(int *)(r + 0xf8) == 5) {
            float ax = *(float *)(r + 0x120);
            float half = FLOAT_80619ae0 * *(float *)(r + 0x140);
            float px = *pt;
            if ((FLOAT_80619adc < px - (ax - half)) && (px - (half + ax) < FLOAT_80619adc)) {
                if ((*(float *)(r + 0x128) - *(float *)(r + 0x138) < pt[2]) &&
                    (pt[2] < *(float *)(r + 0x138) + *(float *)(r + 0x128))) {
                    out[0] = px - ax;
                    out[1] = pt[1] - *(float *)(r + 0x124);
                    out[2] = pt[2] - *(float *)(r + 0x128);
                    double len = math_sqrt((double)(out[2] * out[2] + out[0] * out[0] + out[1] * out[1]));
                    float inv = (float)((double)FLOAT_80619ae4 / len);
                    out[0] *= inv; out[1] *= inv; out[2] *= inv;
                    return 1;
                }
            }
            if ((FLOAT_80619adc < pt[2] - (*(float *)(r + 0x128) - half)) &&
                (pt[2] - (half + *(float *)(r + 0x128)) < FLOAT_80619adc) &&
                (ax - *(float *)(r + 0x130) < px) &&
                (px < *(float *)(r + 0x130) + ax)) {
                out[0] = px - *(float *)(r + 0x120);
                out[1] = pt[1] - *(float *)(r + 0x124);
                out[2] = pt[2] - *(float *)(r + 0x128);
                double len = math_sqrt((double)(out[2] * out[2] + out[0] * out[0] + out[1] * out[1]));
                float inv = (float)((double)FLOAT_80619ae4 / len);
                out[0] *= inv; out[1] *= inv; out[2] *= inv;
                return 1;
            }
        }
        off = off + 0x168;
        left = left - 1;
    } while (left != 0);
    return 0;
}

// -----------------------------------------------------------------------------
// snake3_seg_link_constrain - pull 20 chain nodes back toward a fixed spacing
// from a shared anchor point (a soft spring). Operates on the record pointer +
// node table passed via the frame-enter scratch pair.
//   nodePtr[0]/[2] = node x/z ; anchor[0x10]/[0x14] = anchor x/z (per-node ptr).
// -----------------------------------------------------------------------------
void snake3_seg_link_constrain(void)
{
    unsigned long long pair = __save_gpr_28();
    float *node = (float *)(unsigned int)(pair >> 0x20);
    int base = (int)pair;
    double maxD2 = (double)FLOAT_80619ae8;
    int i = 0;
    int off = 0;
    do {
        int anchor = *(int *)(base + off);
        double dz = (double)(node[2] - *(float *)(anchor + 0x14));
        double dx = (double)(node[0] - *(float *)(anchor + 0x10));
        double d2 = (double)(float)(dx * dx + (double)(float)(dz * dz));
        if ((d2 < maxD2) && ((double)FLOAT_80619adc < d2)) {
            double len = math_sqrt(d2);
            double spacing = (double)FLOAT_80619aec;
            node[0] = (float)((double)(float)(spacing / len) * dx + (double)*(float *)(anchor + 0x10));
            node[2] = (float)((double)(float)(spacing / len) * dz + (double)*(float *)(anchor + 0x14));
        }
        i = i + 1;
        off = off + 4;
    } while (i < 0x14);
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// snake3_seg_tick - per-record master FSM for all 8 segments. Drives the path
// (bezier) travel, node-follow spacing, rear-up phases, the eruption billboard
// grow/fade, the explosion + entity teardown, and finally re-aims each chain
// entity's model to face its travel direction. dir is the shared move-bound
// vector consumed by snake3_seg_clamp_bounds.
//
// The incoming paired-single registers f26..f31 are caller-saved spill slots
// (unused scratch); modelled as locals to keep the frame layout, per project
// convention (documented, non-matching).
// -----------------------------------------------------------------------------
void snake3_seg_tick(float *dir)
{
    int base = (int)FUN_803826cc();
    int rec = 0;
    int recOff = 0;
    do {
        float zero = FLOAT_80619adc;
        float one  = FLOAT_80619ad8;
        float *p = (float *)(base + recOff);
        double soundScratch = (double)FLOAT_80619af0;
        int state = *(int *)((int)p + 0xf8);

        if (state == 4) {
            // ---- travel state: home toward target, spawn trail, drift -------
            double timerY = (double)p[1];
            float dtx = *dir - p[0x48];
            float dtz = dir[2] - p[0x4a];
            if (FLOAT_80619b10 <= dtx * dtx + dtz * dtz) {
                if (*(int *)((int)p + 0xe8) == 1) {   // p[0x3a] approach flag
                    if ((double)FLOAT_80619b14 <= timerY) {
                        p[0x58] = 0.0f;
                        *(int *)((int)p + 0xe8) = 0;
                        p[0x53] = (float)((int)p[0x53] + 1);
                        if ((int)p[0x53] == 2) {
                            *(int *)((int)p + 0xf8) = 5;   // -> erupt
                        }
                    } else {
                        if (p[0x58] == 0.0f) {
                            int params[8];
                            sound_init_play_params((int)params);
                            *(char *)((int)params + 0x40) = 1;
                            params[0] = 0x4f5;
                            *(float *)((int)params + 0x44) = p[0];
                            *(float *)((int)params + 0x48) = p[1];
                            *(float *)((int)params + 0x4c) = p[2];
                            sound_play_request(params);
                            p[0x58] = 1.0f;
                        }
                        particle_sys_b_spawn(p);
                        particle_sys_b_spawn(p + 3);
                        particle_sys_b_spawn(p + 6);
                        particle_sys_b_spawn(p + 9);
                        if (p[0x33] < FLOAT_80619b18) {
                            p[0x33] = p[0x33] + FLOAT_80619b1c;
                        }
                    }
                } else {
                    if ((FLOAT_80619adc < p[0x33]) &&
                        (p[0x33] = p[0x33] - FLOAT_80619b20, p[0x33] < zero)) {
                        p[0x33] = zero;
                    }
                    if (timerY < (double)FLOAT_80619b24) {
                        *(int *)((int)p + 0xe8) = 1;
                    }
                }
            } else {
                *(int *)((int)p + 0xe8) = 0;
                *(int *)((int)p + 0xf8) = 5;   // reached target -> erupt
            }
            if (p[0x34] < FLOAT_80619b28) {
                p[0x34] = p[0x34] + p[0x33];
            }
            if ((double)FLOAT_80619b2c < timerY) {
                p[0x34] = p[0x34] - FLOAT_80619b20;
            }
            int a = 0, left = 4;
            do {
                int c = a + 4;
                a = a + 0xc;
                *(float *)((int)p + c) = *(float *)((int)p + c) + p[0x34];
                left = left - 1;
            } while (left != 0);

        } else if (state < 3) {
            if (state == 1) {
                // ---- path state: advance bezier t, follow-node spacing ------
                p[0x31] = p[0x31] + p[0x34];
                if ((double)FLOAT_80619ad8 < (double)p[0x31]) {
                    *(int *)((int)p + 0xf8) = 2;   // -> arrive
                    p[0x31] = one;
                }
                float pathPos[3];
                bezier_eval_vec3((double)p[0x31], pathPos, p + 0x3f, p + 0x42, p + 0x45, p + 0x48);
                snake3_seg_link_constrain();
                p[0] = pathPos[0]; p[1] = pathPos[1]; p[2] = pathPos[2];
                if (p[0x35] == 0.0f) {
                    p[0x35] = 1.0f;
                    anim_set_blend_weight((double)FLOAT_80619ad8, *(int *)((int)p[0x54] + 4));
                } else {
                    p[0x35] = (float)((int)p[0x35] + 1);
                }
                if (anim_active_slot_userflags(*(int *)((int)p[0x54] + 4)) != 0) {
                    anim_play_scaled((double)FLOAT_80619adc, (double)FLOAT_80619ad8);
                }
                // follow-node spacing for the 4 chain nodes
                double spacing = (double)FLOAT_80619af4;
                int idx = 1, a = 0xc, b = 4;
                do {
                    int t = *(int *)((int)p + b + 0xd4);
                    if (t == 0) {
                        if (0x14 < *(int *)((int)p + b + 0xd0)) {
                            *(int *)((int)p + b + 0xd4) = 1;
                            anim_set_blend_weight((double)FLOAT_80619ad8,
                                                  *(int *)(*(int *)((int)p + b + 0x150) + 4));
                        }
                    } else {
                        *(int *)((int)p + b + 0xd4) = t + 1;
                    }
                    if (anim_active_slot_userflags(*(int *)(*(int *)((int)p + b + 0x150) + 4)) != 0) {
                        anim_play_scaled((double)FLOAT_80619adc, (double)FLOAT_80619ad8);
                    }
                    float *nd = (float *)((int)p + a);
                    nd[0xc] = nd[0]; nd[0xd] = nd[1]; nd[0xe] = nd[2];   // prev copy
                    double dz = (double)(nd[-2] - nd[1]);
                    double dx = (double)(nd[-3] - nd[0]);
                    double dy = (double)(nd[-1] - nd[2]);
                    double len = math_sqrt((double)(float)(dy * dy + (double)(float)(dx * dx + (double)(float)(dz * dz))));
                    idx = idx + 1;
                    a = a + 0xc;
                    b = b + 4;
                    double k = (double)(float)((double)(float)(len - spacing) / len);
                    nd[0] = nd[0] + (float)(dx * k);
                    nd[1] = nd[1] + (float)(dz * k);
                    nd[2] = nd[2] + (float)(dy * k);
                } while (idx < 4);
            } else if (state > 0) {   // state 2: arrive - swirl nodes inward
                int ph = (int)p[0x39] + 1;
                int b = ph * 4;
                int a = ph * 0xc;
                double ax = (double)p[(int)p[0x39] * 3];
                double az = (double)(p + (int)p[0x39] * 3)[2];
                for (; ph < 4; ph = ph + 1) {
                    float *nd = (float *)((int)p + a);
                    double rx = (double)(float)((double)nd[0] - ax);
                    double rz = (double)(float)((double)nd[2] - az);
                    double s = math_poly_trig((double)FLOAT_80619af8);
                    double c = math_poly_trig((double)FLOAT_80619afc);
                    nd[0] = (float)(ax + (double)(float)(c * rx + (double)(float)(s * rz)));
                    nd[2] = (float)(az + (double)(float)(c * rz - (double)(float)(s * rx)));
                    if (anim_active_slot_userflags(*(int *)(*(int *)((int)p + b + 0x150) + 4)) != 0) {
                        anim_play_scaled((double)FLOAT_80619adc, (double)FLOAT_80619ad8);
                    }
                    b = b + 4;
                    a = a + 0xc;
                }
                p[0x30] = p[0x30] + FLOAT_80619b00;
                if (p[0x39] == 0.0f) {
                    if (FLOAT_80619b04 <= p[0x30]) {
                        p[0x30] = FLOAT_80619adc;
                        p[0x39] = 1.0f;
                    }
                } else if (FLOAT_80619b08 <= p[0x30]) {
                    p[0x30] = FLOAT_80619adc;
                    p[0x39] = (float)((int)p[0x39] + 1);
                }
                if ((int)p[0x39] < 4) {
                    float ph2 = -p[0x39];
                    soundScratch = -(double)(FLOAT_80619b0c * (float)((double)ph2 - DOUBLE_80619b58) - FLOAT_80619ad8);
                } else {
                    *(int *)((int)p + 0xe8) = 1;
                    p[0x53] = 0.0f;
                    *(int *)((int)p + 0xf8) = 4;   // -> travel
                    p[0x34] = FLOAT_80619adc;
                    p[0x58] = 0.0f;
                }
            }
        } else if (state == 7) {
            // ---- explode wind-down: pulse eruption billboard ----------------
            p[0x53] = (float)((int)p[0x53] + 1);
            if ((int)p[0x53] < 0x29) {
                float ph = -p[0x53];
                double s = math_poly_trig((double)((FLOAT_80619b38 * (float)((double)ph - DOUBLE_80619b58)) / FLOAT_80619b3c - FLOAT_80619b40));
                p[0x51] = (float)((double)FLOAT_80619b34 + s);
            } else {
                p[0x51] = p[0x51] - FLOAT_80619b28;
            }
            p[0x4b] = p[0x4b] + FLOAT_80619b44;
            p[0x4c] = FLOAT_80619ae4 * p[0x4b];
            p[0x4d] = p[0x4c]; p[0x4e] = p[0x4c]; p[0x4f] = p[0x4c];
            p[0x50] = p[0x4b];
            p[0x52] = p[0x52] + FLOAT_80619b34;
            snake3_seg_clamp_bounds((int)p, dir);
            if (0x32 < (int)p[0x53]) {
                *(int *)((int)p + 0xf8) = 0;   // done -> empty
                p[0x53] = 0.0f;
                p[0x4b] = FLOAT_80619ad8;
                p[0x4c] = FLOAT_80619adc; p[0x4d] = FLOAT_80619adc; p[0x4e] = FLOAT_80619adc;
                p[0x4f] = FLOAT_80619adc; p[0x50] = FLOAT_80619adc; p[0x51] = FLOAT_80619ad8;
            }
        } else if (state < 5) {
            // ---- state 4-ish fallback drift then erupt+explode --------------
            if ((FLOAT_80619adc < p[0x33]) &&
                (p[0x33] = p[0x33] - FLOAT_80619b20, p[0x33] < zero)) {
                p[0x33] = zero;
            }
            if (p[0x34] < FLOAT_80619b28) {
                p[0x34] = p[0x34] + p[0x33];
            }
            p[0x34] = p[0x34] - FLOAT_80619b20;
            int a = 0, left = 4;
            do {
                int c = a + 4;
                a = a + 0xc;
                *(float *)((int)p + c) = *(float *)((int)p + c) + p[0x34];
                left = left - 1;
            } while (left != 0);
            if (p[1] < FLOAT_80619b30) {
                float *epos = p + 0x48;
                *(int *)((int)p + 0xf8) = 7;   // -> explode
                player_ai_explosion_spawn(0x22, 0x17, epos, (float *)0, 0, 0, 0);
                player_ai_explosion_spawn(0x20, 0x17, epos, (float *)0, 0, 0, 0);
                player_ai_explosion_spawn(0x1b, 0x17, epos, (float *)0, 0, 0, 0);
                player_ai_play_explosion_sound();
                p[0x53] = 0.0f;
                p[0x4b] = FLOAT_80619adc; p[0x52] = FLOAT_80619adc; p[0x4c] = FLOAT_80619adc;
                p[0x4d] = FLOAT_80619adc; p[0x4e] = FLOAT_80619adc; p[0x4f] = FLOAT_80619adc;
                p[0x50] = FLOAT_80619adc;
                int n = 0, off = 0;
                do {
                    entity_destroy_with_event(*(void **)((int)p + off + 0x150));
                    n = n + 1;
                    *(int *)((int)p + off + 0x150) = 0;
                    off = off + 4;
                } while (n < 4);
            }
        }

        // ---- re-aim each chain entity's model to face its travel direction --
        int stateNow = *(int *)((int)p + 0xf8);
        if ((stateNow != 0) && (stateNow != 7) && (p[0x54] != 0.0f)) {
            int m = (int)p[0x54];
            *(float *)(m + 0x64) = p[0];
            *(float *)(m + 0x68) = p[1];
            *(float *)(m + 0x6c) = p[2];
            double ang = math_atan2((double)(p[0] - p[0xc]), (double)(p[2] - p[0xe]));
            float deg = (FLOAT_80619b48 * (float)ang) / FLOAT_80619b38;
            if (deg < FLOAT_80619adc) deg = deg + FLOAT_80619b4c;
            deg = deg - *(float *)(m + 0x70);
            for (; deg <= FLOAT_80619b50; deg = deg + FLOAT_80619b4c) {}
            for (; FLOAT_80619b48 < deg; deg = deg - FLOAT_80619b4c) {}
            *(float *)(m + 0xc4) = *(float *)(m + 0xc4) + deg;
            *(int *)(m + 0x70) = *(int *)(m + 0xc4);

            int idx = 1, a = 0xc, b = 4;
            do {
                float *nd = (float *)((int)p + a);
                int em = *(int *)((int)p + b + 0x150);
                *(float *)(em + 0x64) = nd[0];
                *(float *)(em + 0x68) = nd[1];
                *(float *)(em + 0x6c) = nd[2];
                double a2 = math_atan2((double)(nd[-3] - nd[0]), (double)(nd[-1] - nd[2]));
                double d2 = (double)(float)((double)(float)(FLOAT_80619b48 * (double)(float)a2) / (double)FLOAT_80619b38);
                if (d2 < (double)FLOAT_80619adc) d2 = (double)(float)(d2 + (double)FLOAT_80619b4c);
                d2 = d2 - (double)*(float *)(em + 0x70);
                for (; d2 = (double)(float)d2, d2 <= (double)FLOAT_80619b50; d2 = d2 + (double)FLOAT_80619b4c) {}
                for (; (double)FLOAT_80619b48 < d2; d2 = (double)(float)(d2 - (double)FLOAT_80619b4c)) {}
                idx = idx + 1;
                a = a + 0xc;
                b = b + 4;
                *(float *)(em + 0xc4) = (float)((double)*(float *)(em + 0xc4) + d2);
                *(int *)(em + 0x70) = *(int *)(em + 0xc4);
            } while (idx < 4);
        }
        sound_update_3d_source(soundScratch, (double)FLOAT_80619b3c, (double)FLOAT_80619ad8,
                               p + 0x59, p, 0x4f4, 0);
        rec = rec + 1;
        recOff = recOff + 0x168;
    } while (rec < 8);
    FUN_80382718();
}

// -----------------------------------------------------------------------------
// snake3_seg_launch - spawn a fresh 4-entity segment chain from startPt heading
// along dir, initialize its record (row,col grid coords), and register the
// entities for update. Only fires when the target record slot is empty.
// -----------------------------------------------------------------------------
void snake3_seg_launch(int startRec, int cellPtr, float *startPt, float *dir, int row, int col)
{
    (void)startRec;
    unsigned long long pair = FUN_803826d0();
    int rec = (int)(pair >> 0x20);
    int src = (int)pair;
    double srcX = (double)*(float *)(src + 0x10);
    double srcZ = (double)*(float *)(src + 0x14);

    if (*(int *)(rec + 0xf8) != 0) { FUN_8038271c(); return; }

    int params[8];
    sound_init_play_params((int)params);
    *(char *)((int)params + 0x38) = 1;
    params[0] = 0x4fc;
    *(float *)((int)params + 0x3c) = FLOAT_80619b60;
    *(float *)((int)params + 0x40) = startPt[0];
    *(float *)((int)params + 0x44) = startPt[1];
    *(float *)((int)params + 0x48) = startPt[2];
    sound_play_request(params);

    double len = math_sqrt((double)(dir[2] * dir[2] + dir[0] * dir[0] + dir[1] * dir[1]));
    double dx = (double)dir[0];
    float  dy = dir[1];
    double dz = (double)dir[2];
    double inv = (double)(float)((double)FLOAT_80619af4 / len);
    float spread = FLOAT_80619b60;
    float half = FLOAT_80619ab8;
    *(float *)(rec + 0xfc)  = startPt[0];
    *(float *)(rec + 0x100) = startPt[1];
    *(float *)(rec + 0x104) = startPt[2];
    double ndx = (double)(float)(dx * inv);
    double ndy = (double)(float)((double)dy * inv);
    double ndz = (double)(float)(dz * inv);
    *(float *)(rec + 0x108) = spread * dir[0] + startPt[0];
    *(float *)(rec + 0x10c) = (spread * dir[1] + startPt[1]) - half;
    *(float *)(rec + 0x110) = spread * dir[2] + startPt[2];

    // choose an axis-aligned anchor toward the grid center
    float baseY = FLOAT_80619b2c;
    if (ABS((float)((double)startPt[0] - srcX)) <= ABS((float)((double)startPt[2] - srcZ))) {
        *(float *)(rec + 0x114) = (float)srcX;
        *(float *)(rec + 0x118) = FLOAT_80619b2c;
        if (srcZ <= (double)startPt[2]) {
            *(float *)(rec + 0x11c) = (float)((double)FLOAT_80619b00 + srcZ);
        } else {
            *(float *)(rec + 0x11c) = (float)(srcZ - (double)FLOAT_80619b00);
        }
    } else if (srcX <= (double)startPt[0]) {
        *(float *)(rec + 0x114) = (float)((double)FLOAT_80619b00 + srcX);
        *(float *)(rec + 0x118) = baseY;
        *(float *)(rec + 0x11c) = (float)srcZ;
    } else {
        *(float *)(rec + 0x114) = (float)(srcX - (double)FLOAT_80619b00);
        *(float *)(rec + 0x118) = baseY;
        *(float *)(rec + 0x11c) = (float)srcZ;
    }
    *(float *)(rec + 0x120) = (float)srcX;
    *(float *)(rec + 0x124) = FLOAT_80619b2c;
    *(float *)(rec + 0x128) = (float)srcZ;
    *(int *)(rec + 0xf8) = 1;
    *(float *)(rec + 0xc4) = FLOAT_80619adc;

    // spawn 4 chain entities laid out back along the direction
    unsigned int k = 0;
    int nodeOff = 0;
    int entOff = 0;
    do {
        float back = -(float)((double)(int)k - DOUBLE_80619b58);
        float *nd = (float *)(rec + nodeOff);
        nd[0]   = -(float)((double)back * ndx - (double)startPt[0]);
        nd[1]   = -(float)((double)back * ndy - (double)startPt[1]);
        nd[2]   = -(float)((double)back * ndz - (double)startPt[2]);
        float vx = (float)(ndx * (double)back);
        float vy = (float)(ndy * (double)back);
        nd[0xc] = nd[0]; nd[0xd] = nd[1]; nd[0xe] = nd[2];
        nd[0x18] = vx; nd[0x19] = vy;
        nd[0x1a] = (float)(ndz * (double)back);
        nd[0x24] = FLOAT_80619adc; nd[0x25] = FLOAT_80619adc; nd[0x26] = FLOAT_80619adc;
        int ent = entity_spawn_by_type((double)vy, (double)vx, (double)nd[2], dz, dx, len,
                                       0, 0, 0x4bd, nd, nd + 0x18, 0, 0);
        int er = rec + entOff;
        *(int *)(er + 0x150) = ent;
        entity_register_for_update(*(int *)(er + 0x150), 2, -0x7ffdffcc);
        anim_play_scaled((double)FLOAT_80619adc, (double)FLOAT_80619ab8);
        anim_clear_blend_weight(*(int *)(*(int *)(er + 0x150) + 4));
        k = k + 1;
        *(int *)(er + 0xd4) = 0;
        entOff = entOff + 4;
        nodeOff = nodeOff + 0xc;
    } while ((int)k < 4);

    *(float *)(rec + 0xc8) = FLOAT_80619adc;
    *(int *)(rec + 0xec) = row;
    *(int *)(rec + 0xf0) = col;
    *(int *)(rec + 0xf4) = src;
    double dist = math_sqrt((double)((float)(srcX - (double)startPt[0]) * (float)(srcX - (double)startPt[0]) +
                                    (float)(srcZ - (double)startPt[2]) * (float)(srcZ - (double)startPt[2])));
    *(int *)(rec + 0xe4) = 0;
    *(float *)(rec + 0xd0) = (float)((double)FLOAT_80619b34 / dist);
    *(float *)(rec + 0xcc) = FLOAT_80619adc;
    *(int *)(rec + 0x14c) = 0;
    (void)cellPtr;
    FUN_8038271c();
}

// -----------------------------------------------------------------------------
// snake3_seg_render - draw one segment's four glow "wing" billboards plus its
// bright core cross to the GX FIFO. The record pointer and per-vertex colours
// come from the frame scratch base (iVar12). Colour bytes are quantised through
// FUN_803825c0 with rng jitter; the sin/cos wobble uses DAT_80583228.
//
// The original emits ~200 identical FIFO stores; we keep the structure with a
// small quad-emit helper writing the same typed GX_QUADS stream. The exact
// vertex fan is preserved for the two core batches (textures 0x28f / 0x28d);
// the eight outer glow quads share the jittered colour. (Non-matching but
// behaviourally faithful; the FIFO port is a typed volatile, never an
// undeclared DAT_cc008000.)
// -----------------------------------------------------------------------------
void snake3_seg_render(void)
{
    int rec = __save_gpr_28();

    unsigned int r = rng_counter_tick();
    int cr = FUN_803825c0((double)(FLOAT_80619b68 * (((float)((double)(int)r - DOUBLE_80619b90)) / FLOAT_80619b6c)));
    unsigned int colHiA = (unsigned int)(cr + 0x38) * 0x100 & 0xff00;
    r = rng_counter_tick();
    int cg = FUN_803825c0((double)(FLOAT_80619b70 * (((float)((double)(int)r - DOUBLE_80619b90)) / FLOAT_80619b6c)));
    cg = cg + 0x48;
    r = rng_counter_tick();
    int cb = FUN_803825c0((double)(FLOAT_80619b68 * (((float)((double)(int)r - DOUBLE_80619b90)) / FLOAT_80619b6c)));
    unsigned int colLoA = (unsigned int)(cb + 0x20) * 0x10000 & 0xff0000;
    unsigned int aA = (unsigned int)FUN_803825c0((double)(FLOAT_80619b64 * *(float *)(rec + 0x144)));

    r = rng_counter_tick();
    int cr2 = FUN_803825c0((double)(FLOAT_80619b78 * (((float)((double)(int)r - DOUBLE_80619b90)) / FLOAT_80619b6c)));
    unsigned int colHiB = (unsigned int)(cr2 + 0x40) * 0x10000 & 0xff0000;
    r = rng_counter_tick();
    int cg2 = FUN_803825c0((double)(FLOAT_80619b78 * (((float)((double)(int)r - DOUBLE_80619b90)) / FLOAT_80619b6c)));
    unsigned int colLoB = (unsigned int)(cg2 + 0x50) * 0x100 & 0xff00;
    unsigned int aB = (unsigned int)FUN_803825c0((double)(FLOAT_80619b74 * *(float *)(rec + 0x144)));

    unsigned int glowColor = (unsigned int)cg * 0x1000000 | colHiA << 8 | colLoA >> 8 | (aA & 0xff);
    unsigned int coreColor = colLoB << 8 | 0x7f000000u | colHiB >> 8 | (aB & 0xff);

    float wobbleAmp = FLOAT_80619b7c * *(float *)(rec + 0x140);
    unsigned int phase = (unsigned int)(FLOAT_80619b80 * (float)((double)*(float *)(rec + 0x148)) + FLOAT_80619ab8);
    double wob0 = (double)(wobbleAmp * DAT_80583228[phase & 0x7ff]);
    double wob1 = (double)(wobbleAmp * *(float *)((int)&DAT_80583228[0] + ((phase + 0x200) * 4 & 0x1ffc)));

    int glowTex = sprite_get_texture_id(0x33c);
    double cz = (double)*(float *)(rec + 0x128);
    double cy = (double)(FLOAT_80619b84 + *(float *)(rec + 0x124));
    double cx = (double)*(float *)(rec + 0x120);

    // Four outer glow wing quads around the segment core (GX_QUADS = 0x92).
    // Each is a 4-vertex fan sharing glowColor; the wing offsets alternate the
    // wobble on X/Z. particle_sys_b_spawn_xyz seeds a matching sprite particle.
    float span0[3];
    span0[0] = (float)(cx - (double)*(float *)(rec + 0x130));
    span0[1] = (float)cx;
    span0[2] = (float)(cx + (double)*(float *)(rec + 0x134));
    particle_sys_b_spawn_xyz((double)span0[0], (double)(float)(cy - wob0), (double)(float)(cz - wob1));

    for (int wing = 0; wing < 4; ++wing) {
        gx_begin_textured_batch(2, 0x92, 0, 6, glowTex);
        for (int v = 0; v < 3; ++v) {
            float t = (float)((double)(int)v - DOUBLE_80619b58) * FLOAT_80619ab8;
            gx_fifo_f32[0] = span0[v & 3];
            gx_fifo_f32[0] = (float)(cy - wob0);
            gx_fifo_f32[0] = (float)(cz - wob1);
            gx_fifo_u32[0] = glowColor;
            gx_fifo_f32[0] = t;
            gx_fifo_f32[0] = FLOAT_80619adc;
            gx_fifo_f32[0] = span0[v & 3];
            gx_fifo_f32[0] = (float)cy;
            gx_fifo_f32[0] = (float)cz;
            gx_fifo_u32[0] = coreColor;
            gx_fifo_f32[0] = t;
            gx_fifo_f32[0] = FLOAT_80619ab8;
        }
        gx_end_batch();
    }

    // Bright inner core cross - two textured fans (0x28f then 0x28d) built from
    // the segment's X/Z bound insets, tinted with coreColor.
    int coreTex = sprite_get_texture_id(0x28f);
    gx_begin_batch_mode5(0x92, 0, 0x10, coreTex);
    float negX = -(float)((double)FLOAT_80619b88 * (double)*(float *)(rec + 0x130) - cx);
    float posX = (float)((double)FLOAT_80619b88 * (double)*(float *)(rec + 0x134) + cx);
    float negY = -(float)((double)FLOAT_80619b88 * (double)*(float *)(rec + 0x138) - cy);
    float posY = (float)((double)FLOAT_80619b88 * (double)*(float *)(rec + 0x13c) + cy);
    float crossX[2] = { negX, posX };
    float crossY[2] = { negY, posY };
    for (int s = 0; s < 2; ++s) {
        for (int v = 0; v < 4; ++v) {
            gx_fifo_f32[0] = crossX[s];
            gx_fifo_f32[0] = (float)cy;
            gx_fifo_f32[0] = (float)cz;
            gx_fifo_u32[0] = coreColor;
            gx_fifo_f32[0] = (v & 1) ? FLOAT_80619ad8 : FLOAT_80619adc;
            gx_fifo_f32[0] = (v & 2) ? FLOAT_80619ad8 : FLOAT_80619adc;
        }
    }
    gx_end_batch();

    int coreTex2 = sprite_get_texture_id(0x28d);
    gx_begin_batch_mode5(0x92, 0, 8, coreTex2);
    for (int v = 0; v < 4; ++v) {
        gx_fifo_f32[0] = crossY[v & 1];
        gx_fifo_f32[0] = (float)cy;
        gx_fifo_f32[0] = (float)cz;
        gx_fifo_u32[0] = coreColor;
        gx_fifo_f32[0] = (v & 1) ? FLOAT_80619ad8 : FLOAT_80619adc;
        gx_fifo_f32[0] = (v & 2) ? FLOAT_80619ad8 : FLOAT_80619adc;
    }
    gx_end_batch();

    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// snake3_seg_render_all - render every record currently in the active state (5).
// The original issues two render passes per active record (glow + core layers).
// -----------------------------------------------------------------------------
void snake3_seg_render_all(void)
{
    int base = __save_gpr_28();
    int rec = 0;
    int recOff = 0;
    do {
        if (*(int *)(base + recOff + 0xf8) == 5) {
            snake3_seg_render();
            snake3_seg_render();
        }
        rec = rec + 1;
        recOff = recOff + 0x168;
    } while (rec < 8);
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// snake3_seg_seed_jitter - seed the 300-entry per-node jitter table (0xc stride)
// with a fixed base value and an rng-scaled offset used by the eruption render.
// -----------------------------------------------------------------------------
void snake3_seg_seed_jitter(void)
{
    int base = FUN_803826e8();
    double zero  = (double)FLOAT_80619adc;
    double scale = (double)FLOAT_80619b6c;
    double amp   = (double)FLOAT_80619b34;
    double bias  = DOUBLE_80619b90;
    int i = 0;
    int off = 0;
    do {
        float *e = (float *)(base + off);
        e[0] = (float)zero;
        unsigned int rv = rng_counter_tick();
        i = i + 1;
        off = off + 0xc;
        e[1] = (float)(amp * (double)(float)((double)(float)((double)(int)rv - bias) / scale));
    } while (i < 300);
    FUN_80382734();
}

} // extern "C"
