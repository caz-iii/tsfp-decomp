// Address: 0x80357cc4 | Name: cloak_refraction_reset      | Date: 2026-09-12
// Address: 0x80357ce4 | Name: cloak_gather_entities       | Date: 2026-09-12
// Address: 0x80357dd8 | Name: cloak_refresh_entity_list   | Date: 2026-09-12
// Address: 0x80357e28 | Name: cloak_entity_onscreen       | Date: 2026-09-12
// Address: 0x80358104 | Name: cloak_mark_collision_flags  | Date: 2026-09-12
// Address: 0x803581d0 | Name: cloak_refraction_render     | Date: 2026-09-12
//
// Cloak / active-camouflage screen-space refraction effect. Invisible entities
// (render flag bit 0x8000000, or arcade cloak object types 8 / 0x1000) are
// gathered into DAT_805e3960 (0xc-byte entries, handle at +0). Once per frame
// the list is refreshed; during the HUD render pass each on-screen cloaked
// entity is drawn into a distortion buffer and a screen-space heat-haze warp is
// applied over its region (sprite 0x333 as the distortion map).
//   DAT_80612e08/e10/e14 : effect state (active id, last-frame stamp, count)
//   FLOAT_80612e0c        : near-plane threshold
//   DAT_80611c24          : entity table (0x2e8-stride, gen id at +0x2c)

#include "common.h"

// ---- effect state ----------------------------------------------------------
extern int   DAT_80612e08;      // active effect id (-1 none)
extern float FLOAT_80612e0c;    // near-plane cull threshold
extern int   DAT_80612e10;      // last-refresh frame stamp
extern int   DAT_80612e14;      // gathered entity count
extern int   DAT_805e3960;      // gathered entity list base (0xc stride)

// ---- globals ---------------------------------------------------------------
extern int   DAT_80611c24;      // entity table base
extern int   DAT_80611c70;      // entity table count
extern int   DAT_80611928;      // frame counter
extern int   DAT_80611d54;      // active camera/player ctx
extern int   DAT_80611d74;      // local-player ctx (own entity at +0x14)
extern int   DAT_80611748;      // viewport/render ctx (int[])
extern unsigned int DAT_806109c4; // screen width
extern unsigned int DAT_806109c8; // screen height
extern char  DAT_805015bd;      // current level id
extern int   DAT_80612610;      // level sub-state
extern int   DAT_806108e4;      // distortion-enabled flag
extern int   DAT_803e0c00;      // clip-flag scratch template
extern int   DAT_803e0c04;
extern int   DAT_803e0c08;
extern int   DAT_803e0c0c;
extern int   DAT_803e0c10;
extern int   DAT_803e0c14;

// ---- float / double constants ----------------------------------------------
extern float  FLOAT_8061ae70;   // near threshold base
extern float  FLOAT_8061ae74;   // projection scale
extern double DOUBLE_8061ae78;  // int->double bias
extern double DOUBLE_8061ae88;  // int->double bias (alt)
extern float  FLOAT_8061ae80;   // jitter amplitude
extern float  FLOAT_8061ae84;   // jitter denom

// ---- helpers ---------------------------------------------------------------
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern int  FUN_803826e8(void);
extern void FUN_80382734(void);
extern void FUN_803826d4(void);
extern void FUN_80382720(void);
extern int  game_is_standard_arcade(void);
extern void entity_compute_render_aabb(void);
extern void mtx4_transform_point4_alias(float* mtx, float* in, float* out);
extern int  rng_counter_tick(void);
extern void collision_set_flags(int* col, int flags);
extern void collision_clear_flags(int col, int flags);
extern unsigned long long collision_set_shape_params(int* col, int a, int b, int c, int d);
extern void render_instance_draw_multipass(unsigned long long a, double b, unsigned int c);
extern void render_instance_clear_visual_state(int col);
extern int  sprite_get_texture_id(int ref);
extern void gx_set_sub_material_id(int id);
extern void gx_set_blend_mode(int mode);
extern void gx_set_active_material(int mat);
extern void gx_fill_rect_clipped(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, int f);
extern void gx_fill_rect_solid(double a, double b, double c, double d,
                               unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
extern void gx_get_scissor_rect(void* a, void* b, int* c, int* d);
extern void gx_set_scissor_rect(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
extern void gx_draw_sprite_rect(unsigned int a, unsigned int b, unsigned int c, unsigned int d,
                                int e, int f, int g, int h);
extern int  gx_set_material_override_cb(int cb);
extern void gx_tev_distort_teardown(void);

// forward decls (consumed return values within this file)
int cloak_gather_entities(void);
int cloak_entity_onscreen(void);

// ============================================================================
// cloak_refraction_reset  (0x80357cc4)
// ============================================================================
void cloak_refraction_reset(void)
{
    DAT_80612e08 = -1;
    FLOAT_80612e0c = FLOAT_8061ae70;
    DAT_80612e14 = 0;
    DAT_80612e10 = -1;
}

// ============================================================================
// cloak_gather_entities  (0x80357ce4)
// Scan the entity table for cloaked/invisible entities (owner flag 0x8000000,
// or arcade cloak object types 8 / 0x1000 outside standard arcade) that are
// active (flag bit 1) and either a cloak type or beyond the near threshold, and
// record their handles into the caller-provided list (capacity in r-save low
// word). Returns the number gathered.
// ============================================================================
int cloak_gather_entities(void)
{
    unsigned long long saved = __save_gpr_25();
    int* outList = (int*)(unsigned int)(saved >> 0x20);
    int capacity = (int)saved;

    int count = 0;
    int outOff = 0;
    int entOff = 0;
    for (int i = 0; i < DAT_80611c70; i++) {
        int ent = DAT_80611c24 + entOff;
        if (((*(unsigned int*)(ent + 0x28) & 0x80000000) == 0) &&
            ((((*(unsigned int*)(*(int*)(ent + 0x24) + 0x28) & 0x8000000) != 0) ||
              ((game_is_standard_arcade() == 0) &&
               ((*(int*)(ent + 0x20) == 8) || (*(int*)(ent + 0x20) == 0x1000)))) &&
             ((*(unsigned int*)(ent + 0x28) & 1) != 0)) &&
            (((*(int*)(ent + 0x20) == 8) || (*(int*)(ent + 0x20) == 0x1000)) ||
             (FLOAT_8061ae70 < *(float*)(ent + 0x1cc)))) {
            count += 1;
            *(int*)((int)outList + outOff) = *(int*)(ent + 0x2c);
            outOff += 0xc;
            if (count == capacity) break;
        }
        entOff += 0x2e8;
    }
    __restore_gpr_25();
    return count;
}

// ============================================================================
// cloak_refresh_entity_list  (0x80357dd8)
// Refresh the gathered cloak-entity list at most once per frame.
// ============================================================================
void cloak_refresh_entity_list(void)
{
    FLOAT_80612e0c = FLOAT_8061ae74;
    if (DAT_80612e10 < DAT_80611928) {
        DAT_80612e14 = cloak_gather_entities();
        DAT_80612e10 = DAT_80611928;
    }
}

// ============================================================================
// cloak_entity_onscreen  (0x80357e28)
// Frustum/viewport test for a cloak entity's render AABB: project its 8 corners
// to screen space, and return non-zero when the projected box overlaps the
// viewport rect (and it is not fully behind the near plane). Returns 0 when the
// box is on-screen (matching the original's countLeadingZeros(flag) idiom where
// flag 0 => visible), i.e. reports "should render".
// ============================================================================
int cloak_entity_onscreen(void)
{
    int self = FUN_803826e8();
    bool allBehind = true;
    entity_compute_render_aabb();
    double nearZ = (double)FLOAT_8061ae70;

    float corners[32];      // 8 corners x 4
    float projected[32];
    int idx = 0;
    for (int i = 0; i < 8; i++) {
        mtx4_transform_point4_alias(*(float**)(DAT_80611d54 + 0x20c),
                                    (float*)((int)corners + idx),
                                    (float*)((int)projected + idx));
        if (nearZ <= (double)*(float*)((int)projected + idx + 0xc)) {
            allBehind = false;
        }
        idx += 0x10;
    }

    unsigned int result = 1;   // default: not visible
    if (!allBehind) {
        result = 1;
        if (*(int*)(self + 0x20) != 2) {
            int cx0 = DAT_803e0c00, cx1 = DAT_803e0c04, cx2 = DAT_803e0c08;
            int cy0 = DAT_803e0c0c, cy1 = DAT_803e0c10, cy2 = DAT_803e0c14;
            int off = 0;
            int n = 8;
            do {
                float w = ABS(*(float*)((int)projected + off + 0xc));
                float inv = FLOAT_8061ae70;
                if (FLOAT_8061ae70 < w) inv = FLOAT_8061ae74 / w;

                float sx = inv * (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x10) ^ 0x80000000) - DOUBLE_8061ae78) *
                                 *(float*)((int)projected + off) +
                           (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_8061ae78);
                float sy = -(inv * (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x24) ^ 0x80000000) - DOUBLE_8061ae78) *
                                   *(float*)((int)projected + off + 4) -
                             (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x1c) ^ 0x80000000) - DOUBLE_8061ae78));

                float vx0 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)DAT_80611748 ^ 0x80000000) - DOUBLE_8061ae78);
                float vx1 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 4) ^ 0x80000000) - DOUBLE_8061ae78);
                if (vx0 <= sx) {
                    if (sx <= vx1) cx1 = 1; else cx2 = 1;
                } else {
                    cx0 = 1;
                }
                float vy0 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x14) ^ 0x80000000) - DOUBLE_8061ae78);
                float vy1 = (float)((double)CONCAT44(0x43300000, (unsigned int)*(int*)(DAT_80611748 + 0x18) ^ 0x80000000) - DOUBLE_8061ae78);
                if (vy0 <= sy) {
                    if (sy <= vy1) cy1 = 1; else cy2 = 1;
                } else {
                    cy0 = 1;
                }
                if (((cx1 != 0) || ((cx0 != 0) && (cx2 != 0))) &&
                    ((cy1 != 0) || ((cy0 != 0) && (cy2 != 0)))) {
                    result = 0;   // on-screen
                    break;
                }
                off += 0x10;
                n -= 1;
            } while (n != 0);
        }
        countLeadingZeros(result);
    }
    FUN_80382734();
    return (int)result == 0 ? 1 : 0;
}

// ============================================================================
// cloak_mark_collision_flags  (0x80358104)
// Mark every valid, non-self, non-hidden cloak entity's collision object with
// flag 8 (participates in the distortion pass), then raise the camera ctx's
// distortion-request bit 0x8000.
// ============================================================================
void cloak_mark_collision_flags(void)
{
    FUN_803826f0();
    int off = 0;
    for (int i = 0; i < DAT_80612e14; i++) {
        unsigned int handle = *(unsigned int*)((int)&DAT_805e3960 + off);
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) {
                ent = 0;
                *(int*)((int)&DAT_805e3960 + off) = 0;
            }
        }
        if (ent != 0) {
            if ((ent != *(int*)(DAT_80611d74 + 0x14)) && ((*(unsigned int*)(ent + 0x28) & 0x800000) == 0)) {
                collision_set_flags(*(int**)(ent + 4), 8);
            }
        }
        off += 0xc;
    }
    *(unsigned int*)(DAT_80611d54 + 0x21c) = *(unsigned int*)(DAT_80611d54 + 0x21c) | 0x8000;
    FUN_8038273c();
}

// ============================================================================
// cloak_refraction_render  (0x803581d0)
// The heat-haze / cloak distortion render. Clips to the viewport, draws each
// on-screen cloak entity into the distortion buffer via a multipass render,
// then applies a screen-space warp using sprite 0x333 as the distortion map,
// with per-frame random UV jitter, and finally clears each entity's visual
// state. Guarded by the distortion-enabled flag.
// ============================================================================
void cloak_refraction_render(void)
{
    FUN_803826d4();
    unsigned int y1 = (unsigned int)*(int*)(DAT_80611748 + 0x18);
    unsigned int x0 = (unsigned int)*(int*)DAT_80611748;
    unsigned int x1 = (unsigned int)*(int*)(DAT_80611748 + 4) + 1;
    unsigned int y0 = (unsigned int)*(int*)(DAT_80611748 + 0x14);
    unsigned int yb = y1 + 1;

    if (DAT_806108e4 == 0) { FUN_80382720(); return; }

    int jx, jy;
    {
        int r = rng_counter_tick();
        jx = (int)(FLOAT_8061ae80 * ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae88) / FLOAT_8061ae84));
    }
    double warp;
    {
        int r = rng_counter_tick();
        warp = (double)(float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae88);
        jy = (int)(FLOAT_8061ae80 * (float)(warp / (double)FLOAT_8061ae84));
    }

    if ((DAT_805015bd == '\n') && (DAT_80612610 == 1)) {
        y0 = y0 + 0x40;
        yb = y1 - 0x3f;
    }

    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    gx_set_sub_material_id(0xb);
    gx_fill_rect_clipped(x0, y0, x1, yb, 0x5f);

    int sc0; int sc1; int sc2; int sc3;
    gx_get_scissor_rect(&sc0, &sc1, &sc2, &sc3);
    gx_set_scissor_rect(x0, y0, x1, yb);
    gx_set_sub_material_id(1);

    int off = 0;
    for (int i = 0; i < DAT_80612e14; i++) {
        unsigned int handle = *(unsigned int*)((int)&DAT_805e3960 + off);
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) {
                ent = 0;
                *(int*)((int)&DAT_805e3960 + off) = 0;
            }
        }
        if ((ent != 0) && (ent != *(int*)(DAT_80611d74 + 0x14)) && (cloak_entity_onscreen() != 0)) {
            *(unsigned int*)(ent + 0x10) = *(unsigned int*)(ent + 0x10) | (1 << *(int*)(DAT_80611d54 + 0x214));
            collision_clear_flags(*(int*)(ent + 4), 8);
            unsigned long long shape = collision_set_shape_params(*(int**)(ent + 4), 0x10, -1, 0, 0xff);
            render_instance_draw_multipass(shape, warp, *(unsigned int*)(ent + 4));
        }
        off += 0xc;
    }

    gx_set_sub_material_id(3);
    int tex = sprite_get_texture_id(0x333);
    gx_set_active_material(tex);
    gx_draw_sprite_rect(x0, y0, x1, yb, x0 * 0x10 + jx, y0 * 0x10 + jy, x1 * 0x10 + jx, yb * 0x10 + jy);

    int savedCb = gx_set_material_override_cb(-0x7fe717b0);
    gx_set_sub_material_id(1);
    gx_fill_rect_solid(
        (double)((float)((double)CONCAT44(0x43300000, x0 ^ 0x80000000) - DOUBLE_8061ae78) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_8061ae78)),
        (double)((float)((double)CONCAT44(0x43300000, y0 ^ 0x80000000) - DOUBLE_8061ae78) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_8061ae78)),
        (double)((float)((double)CONCAT44(0x43300000, x1 ^ 0x80000000) - DOUBLE_8061ae78) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c4 ^ 0x80000000) - DOUBLE_8061ae78)),
        (double)((float)((double)CONCAT44(0x43300000, yb ^ 0x80000000) - DOUBLE_8061ae78) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c8 ^ 0x80000000) - DOUBLE_8061ae78)),
        x0, y0, x1, yb);
    gx_tev_distort_teardown();
    gx_set_material_override_cb(savedCb);
    gx_set_scissor_rect((unsigned int)sc0, (unsigned int)sc1, (unsigned int)sc2, (unsigned int)sc3);

    gx_fill_rect_clipped(x0, 0, x1, y0, 0x7f);
    gx_fill_rect_clipped(x0, yb, x1, DAT_806109c8, 0x7f);

    off = 0;
    for (int i = 0; i < DAT_80612e14; i++) {
        unsigned int handle = *(unsigned int*)((int)&DAT_805e3960 + off);
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) {
                ent = 0;
                *(int*)((int)&DAT_805e3960 + off) = 0;
            }
        }
        if (ent != 0) {
            render_instance_clear_visual_state(*(int*)(ent + 4));
        }
        off += 0xc;
    }
    FUN_80382720();
}
