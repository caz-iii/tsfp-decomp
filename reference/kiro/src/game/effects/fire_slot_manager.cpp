// Address: 0x8035aad4 | Name: fire_slot_render_glow          | Date: 2026-09-12
// Address: 0x8035ad14 | Name: fire_render_screen_flash       | Date: 2026-09-12
// Address: 0x8035ae48 | Name: fire_render_distort_pass       | Date: 2026-09-12
// Address: 0x8035af44 | Name: fire_render_dispatch           | Date: 2026-09-12
// Address: 0x8035af68 | Name: fire_slots_init                | Date: 2026-09-12
// Address: 0x8035afec | Name: fire_slots_render_glow_all     | Date: 2026-09-12
// Address: 0x8035b0c0 | Name: fire_slots_render_scene        | Date: 2026-09-12
// Address: 0x8035b0e4 | Name: fire_slots_reset               | Date: 2026-09-12
// Address: 0x8035b0f8 | Name: fire_slot_add                  | Date: 2026-09-12
// Address: 0x8035b1c0 | Name: fire_slot_add_single           | Date: 2026-09-12
//
// Fire-effect slot manager. Owns a 32-entry pool of burning-entity effect slots
// (DAT_805e40f8, 0x74-byte records, live count DAT_80612e1c) plus the shared
// screen-flash intensity FLOAT_80612e18. Each slot record:
//   +0x00        entity handle 0 (burning subject)
//   +0x04        entity handle 1 (secondary, optional)
//   +0x08..0x1c  skeleton flame node indices [6]
//   +0x18        flash contribution (float)
//   +0x1c        render-target texture id (from render_target_alloc_scratch)
//   +0x20        alpha/heat scalar
//   +0x24 (0x2c) scratch RT handle
//   +0x40..0x5c  two screen rects (mask rect / warp rect)
//   +0x68        animated heat phase
//   +0x70        active flag
// The renderers themselves live in fire_distortion.cpp (batch_1016); this file
// is the per-frame driver, called from the session render/tick/setup paths.

#include "common.h"

// ---- slot pool -------------------------------------------------------------
extern int   DAT_805e40f8;   // slot pool base (0x74 stride, this is field +0x00)
extern int   DAT_805e40fc;   // slot field +0x04 (handle 1)
extern int   DAT_805e4100;   // slot field +0x08 (skeleton ctx ptr)
extern int   DAT_805e4104;   // slot field +0x0c (node index [0])
extern float DAT_805e4118;   // slot field +0x20 (alpha/heat scalar)
extern int   DAT_805e411c;   // slot field +0x24 (render-enabled flag == 1)
extern int   DAT_805e4120;   // slot field +0x28 (owning render ctx)
extern int   DAT_805e4124;   // slot field +0x2c (scratch RT handle)
extern int   DAT_80612e1c;   // live slot count
extern float FLOAT_80612e18; // screen-flash intensity accumulator

// ---- globals ---------------------------------------------------------------
extern int DAT_80610a20;     // sprite/material table ctx (int[])
extern int DAT_80612254;     // bound material ptr
extern int DAT_80612614;     // current level id
extern unsigned int DAT_806109c4; // screen width

// ---- float / double constants ----------------------------------------------
extern float  FLOAT_8061af1c, FLOAT_8061af28, FLOAT_8061af58, FLOAT_8061af84;
extern float  FLOAT_8061af70, FLOAT_8061af74, FLOAT_8061af78, FLOAT_8061af7c, FLOAT_8061af80;
extern double DOUBLE_8061af40;

// ---- helpers ---------------------------------------------------------------
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  FUN_803826f0(void);   // render scope push (returns ctx)
extern void FUN_8038273c(void);   // render scope pop
extern unsigned long long FUN_803826e4(void);  // add-slot ctx fetch (packed ctx/flag)
extern void FUN_80382730(void);   // scope pop (add)
extern void gx_bind_material_fallback(void);
extern int  gx_set_blend_func_custom(int a, int b, int c, int d, int e, int f);
extern int  gx_set_blend_mode(int mode);
extern int  gx_set_sub_material_id(int id);
extern void gx_set_color_update_mode(void);
extern void gx_material_state_nop(void);
extern void gx_draw_quad_pos4(float* p0, float* p1, float* p2, float* p3,
                              unsigned int c0, unsigned int c1, unsigned int c2, unsigned int c3);
extern void memset_wrapper(int dst, int val, int len);
extern void render_target_alloc_scratch(int w, int h, int* outTex);

// Renderers from fire_distortion.cpp (signatures match the definitions there;
// also emitted into generated_decls.h).
extern void fire_render_glow_strips(double left, double centerY, double right,
                                    double z, double alpha, unsigned int p7, unsigned int p8,
                                    int u1, int v1, int material, int rows);   // 11 params
extern void fire_render_flame_column(double left, double topY, double right,
                                     double z, double intensityScale, unsigned int p7, unsigned int p8,
                                     unsigned int p9, unsigned int p10, int ctx); // 10 params
extern void fire_distortion_render(unsigned long long a1, unsigned long long a2,
                                   unsigned int p3, unsigned int p4, unsigned int p5,
                                   unsigned int p6, unsigned int p7, unsigned int p8);

// forward decls
void fire_slot_render_glow(void);
void fire_render_screen_flash(void);
void fire_render_distort_pass(unsigned long long a1, unsigned long long a2, unsigned int p3,
                              unsigned int p4, unsigned int p5, unsigned int p6,
                              unsigned int p7, unsigned int p8);
void fire_slots_render_glow_all(void);

// ============================================================================
// fire_slot_render_glow  (0x8035aad4)
// Render the additive glow + flame-column passes for the current slot (ctx in
// the r-save), binding the slot material and using level-specific blend for
// level 0x52. Guarded by the slot active flag (+0x70).
// ============================================================================
void fire_slot_render_glow(void)
{
    int slot = __save_gpr_28();
    if (*(int*)(slot + 0x70) != 0) {
        int tex = *(int*)(slot + 0x2c);
        double alpha = (double)(FLOAT_8061af28 - *(float*)(slot + 0x20));
        int* stab = (int*)DAT_80610a20;
        if ((tex < 0) || (stab[3] <= tex) || (tex <= stab[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *(int*)DAT_80610a20 + tex * 0x58;
        }
        int savedBlend = gx_set_blend_func_custom(7, 0, 0, 0, 2, 1);
        if (DAT_80612614 == 0x52) {
            gx_set_blend_func_custom(7, 0, 0, 0, 0, 1);
        }
        int savedSub = gx_set_sub_material_id(1);
        gx_set_color_update_mode();

        double l = (double)(float)((double)CONCAT44(0x43300000, *(unsigned int*)(slot + 0x40) ^ 0x80000000) - DOUBLE_8061af40);
        double t = (double)(float)((double)CONCAT44(0x43300000, *(unsigned int*)(slot + 0x44) ^ 0x80000000) - DOUBLE_8061af40);
        double r = (double)(float)((double)CONCAT44(0x43300000, *(unsigned int*)(slot + 0x48) ^ 0x80000000) - DOUBLE_8061af40);
        double b = (double)(float)((double)CONCAT44(0x43300000, *(unsigned int*)(slot + 0x4c) ^ 0x80000000) - DOUBLE_8061af40);
        double heat = (double)*(float*)(slot + 0x68);

        // heat (+0x68) is the intensity/phase scalar; alpha (1 - +0x20) is the fade.
        fire_render_glow_strips(l, t, r, b, heat,
                                *(unsigned int*)(slot + 0x50), *(unsigned int*)(slot + 0x54),
                                *(int*)(slot + 0x58), *(int*)(slot + 0x5c), *(int*)(slot + 0x2c), 0x10);
        gx_set_sub_material_id(2);
        fire_render_flame_column(l, t, r, b, heat,
                                 *(unsigned int*)(slot + 0x50), *(unsigned int*)(slot + 0x54),
                                 *(unsigned int*)(slot + 0x58), *(unsigned int*)(slot + 0x5c), *(int*)(slot + 0x2c));
        (void)alpha;
        gx_set_blend_mode(savedBlend);
        gx_set_sub_material_id(savedSub);
        gx_set_color_update_mode();
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_render_screen_flash  (0x8035ad14)
// Draw a full-screen additive white flash quad whose alpha tracks the shared
// flash intensity FLOAT_80612e18 (clamped 0..0x7f). Used when a large fire
// event raises the flash accumulator.
// ============================================================================
void fire_render_screen_flash(void)
{
    double intensity = (double)FUN_803826f0();
    int savedBlend = gx_set_blend_mode(2);
    int savedSub = gx_set_sub_material_id(2);
    int a = (int)((double)FLOAT_8061af58 * intensity);
    int clamped = (a < 0x80) ? a : 0x7f;
    if (clamped < 0) clamped = 0; else clamped = (a < 0x80) ? a : 0x7f;
    unsigned int color = clamped << 0x18 | 0xffffff;

    float q0[3] = { FLOAT_8061af70, FLOAT_8061af74, FLOAT_8061af78 };
    float q1[3] = { FLOAT_8061af70, FLOAT_8061af74, FLOAT_8061af7c };
    float q2[3] = { FLOAT_8061af70, FLOAT_8061af80, FLOAT_8061af78 };
    float q3[3] = { FLOAT_8061af70, FLOAT_8061af80, FLOAT_8061af7c };

    gx_material_state_nop();
    gx_draw_quad_pos4(q0, q1, q2, q3, color, color, color, color);
    gx_material_state_nop();
    gx_set_blend_mode(savedBlend);
    gx_set_sub_material_id(savedSub);
    FUN_8038273c();
}

// ============================================================================
// fire_render_distort_pass  (0x8035ae48)
// Per-frame distortion render walk: for every slot owned by the current render
// ctx, clear bit 1 on each active flame node's material flag, run
// fire_distortion_render when the slot is render-enabled, then restore the bit.
// ============================================================================
void fire_render_distort_pass(unsigned long long a1, unsigned long long a2, unsigned int p3,
                              unsigned int p4, unsigned int p5, unsigned int p6,
                              unsigned int p7, unsigned int p8)
{
    int ctx = __save_gpr_28();
    for (int i = DAT_80612e1c - 1; i >= 0; i--) {
        int base = i * 0x74;
        if (*(int*)((int)&DAT_805e4120 + base) == ctx) {
            for (int j = 5; j >= 0; j--) {
                int skel = *(int*)((int)&DAT_805e4100 + base);
                int node;
                if ((skel != 0) && (node = *(int*)((int)&DAT_805e40f8 + j * 4 + 0xc + base), 0 < node)) {
                    node = node * 0x2c + 0x58;
                    *(unsigned int*)(skel + node) = *(unsigned int*)(skel + node) & 0xfffffffe;
                }
            }
            if (*(int*)((int)&DAT_805e411c + base) == 1) {
                fire_distortion_render(a1, a2, p3, p4, p5, p6, p7, p8);
            }
            for (int j = 5; j >= 0; j--) {
                int skel = *(int*)((int)&DAT_805e4100 + base);
                int node;
                if ((skel != 0) && (node = *(int*)((int)&DAT_805e40f8 + j * 4 + 0xc + base), 0 < node)) {
                    node = node * 0x2c + 0x58;
                    *(unsigned int*)(skel + node) = *(unsigned int*)(skel + node) | 1;
                }
            }
        }
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_render_dispatch  (0x8035af44)
// Thin forwarder to the distortion render pass (session render entry point).
// ============================================================================
void fire_render_dispatch(unsigned long long a1, unsigned long long a2, unsigned int p3,
                          unsigned int p4, unsigned int p5, unsigned int p6,
                          unsigned int p7, unsigned int p8)
{
    fire_render_distort_pass(a1, a2, p3, p4, p5, p6, p7, p8);
}

// ============================================================================
// fire_slots_init  (0x8035af68)
// Clear the whole 32-slot pool, reset the flash accumulator, and allocate an
// 8x8 scratch render target per slot. Called from session_setup_by_mode.
// ============================================================================
void fire_slots_init(void)
{
    FUN_803826f0();
    DAT_80612e1c = 0;
    FLOAT_80612e18 = FLOAT_8061af84;
    memset_wrapper(-0x7fa1bf08, 0, 0xe80);   // &DAT_805e40f8 pool, 32 * 0x74
    int off = 0;
    for (int i = 0; i < 0x20; i++) {
        render_target_alloc_scratch(8, 8, (int*)((int)&DAT_805e4124 + off));
        off += 0x74;
    }
    FUN_8038273c();
}

// ============================================================================
// fire_slots_render_glow_all  (0x8035afec)
// Per-frame glow/flame walk: for each slot owned by the current ctx, clear the
// node material bit, run fire_slot_render_glow when enabled, restore, then draw
// the screen flash if the flash accumulator is positive.
// ============================================================================
void fire_slots_render_glow_all(void)
{
    int ctx = __save_gpr_28();
    for (int i = DAT_80612e1c - 1; i >= 0; i--) {
        int base = i * 0x74;
        gx_material_state_nop();
        if (*(int*)((int)&DAT_805e4120 + base) == ctx) {
            for (int j = 5; j >= 0; j--) {
                int skel = *(int*)((int)&DAT_805e4100 + base);
                if ((skel != 0) && (0 < *(int*)((int)&DAT_805e4104 + base + j * 4))) {
                    int node = *(int*)((int)&DAT_805e4104 + base + j * 4) * 0x2c + 0x58;
                    *(unsigned int*)(skel + node) = *(unsigned int*)(skel + node) & 0xfffffffe;
                }
            }
            if (*(int*)((int)&DAT_805e411c + base) == 1) {
                fire_slot_render_glow();
            }
        }
    }
    if (FLOAT_8061af1c < FLOAT_80612e18) {
        fire_render_screen_flash();
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_slots_render_scene  (0x8035b0c0)
// Session scene-render entry: forwards to the glow/flame walk.
// ============================================================================
void fire_slots_render_scene(void)
{
    fire_slots_render_glow_all();
}

// ============================================================================
// fire_slots_reset  (0x8035b0e4)
// Drop all slots and reset the flash accumulator (per-frame/level reset).
// ============================================================================
void fire_slots_reset(void)
{
    DAT_80612e1c = 0;
    FLOAT_80612e18 = FLOAT_8061af84;
}

// ============================================================================
// fire_slot_add  (0x8035b0f8)
// Register a new fire slot for a burning entity (and optional secondary), if the
// pool is not full. Preserves the pre-allocated scratch RT handle, records the
// owning ctx / render-enabled flag / initial heat from the add ctx, and stores
// the two entity handles.
// ============================================================================
void fire_slot_add(unsigned int a1, unsigned int a2, int ent0, int ent1)
{
    unsigned long long ctx = FUN_803826e4();
    if ((DAT_80612e1c < 0x20) && (ent0 != 0)) {
        int base = DAT_80612e1c * 0x74;
        int savedRt = *(int*)((int)&DAT_805e4124 + base);
        float heat = *(float*)&ctx;   // low f1 alias in original (heat scalar)
        memset_wrapper((int)((int)&DAT_805e40f8 + base), 0, 0x74);
        *(int*)((int)&DAT_805e4124 + base) = savedRt;
        *(int*)((int)&DAT_805e411c + base) = (int)(ctx >> 0x20);
        *(float*)((int)&DAT_805e4118 + base) = heat;
        *(int*)((int)&DAT_805e4120 + base) = (int)ctx;
        *(int*)((int)&DAT_805e40f8 + base) = *(int*)(ent0 + 0x2c);
        if (ent1 != 0) {
            *(int*)((int)&DAT_805e40fc + base) = *(int*)(ent1 + 0x2c);
        }
        DAT_80612e1c = DAT_80612e1c + 1;
    }
    FUN_80382730();
}

// ============================================================================
// fire_slot_add_single  (0x8035b1c0)
// Convenience wrapper: add a fire slot for a single burning entity.
// ============================================================================
void fire_slot_add_single(unsigned int a1, int ent0)
{
    fire_slot_add(a1, 0, ent0, 0);
}

// ---------------------------------------------------------------------------
// batch_1018 additions: two more slot-add variants + the flash-intensity setter.
// ---------------------------------------------------------------------------

// Address: 0x8035b1ec | Name: fire_slot_add_nodes         | Date: 2026-09-12
// Address: 0x8035b2fc | Name: fire_slot_add_collision     | Date: 2026-09-12
// Address: 0x8035b414 | Name: fire_set_flash_intensity    | Date: 2026-09-12

extern unsigned long long FUN_803826e8(void);   // active skeleton ctx (packed: ctx in low word)
extern void FUN_80382734(void);                 // render scope pop (add-nodes variant)
extern void collision_set_flags(int* obj, unsigned short flags);

// ============================================================================
// fire_slot_add_nodes  (0x8035b1ec)
// Register a fire slot for the active render-ctx skeleton (ctx from the shared
// fetch), enabling burn rendering on up to 5 skeleton flame nodes listed in
// nodeList (positive node indices). Sets material bit 1 on each and records the
// index. Called from cutscene_prop_script_tick / worm_results_outro_tick.
// ============================================================================
void fire_slot_add_nodes(unsigned int a1, unsigned int a2, int nodeList)
{
    unsigned long long packed = FUN_803826e8();
    int skel = (int)packed;
    (void)a1; (void)a2;
    if ((DAT_80612e1c < 0x20) && (skel != 0)) {
        int base = DAT_80612e1c * 0x74;
        int savedRt = *(int*)((int)&DAT_805e4124 + base);
        float heat = *(float*)&packed;   // f1 alias (initial heat)
        memset_wrapper((int)((int)&DAT_805e40f8 + base), 0, 0x74);
        *(int*)((int)&DAT_805e4124 + base) = savedRt;
        *(int*)((int)&DAT_805e411c + base) = (int)(packed >> 0x20);
        *(float*)((int)&DAT_805e4118 + base) = heat;
        *(int*)((int)&DAT_805e4100 + base) = skel;
        if (skel != 0) {
            for (int j = 5; --j >= 0; ) {
                int off = j * 4;
                if (0 < *(int*)(nodeList + off)) {
                    int node = *(int*)(nodeList + off) * 0x2c + 0x58;
                    int s = *(int*)((int)&DAT_805e4100 + DAT_80612e1c * 0x74);
                    *(unsigned int*)(s + node) = *(unsigned int*)(s + node) | 1;
                    *(int*)((int)&DAT_805e4104 + DAT_80612e1c * 0x74 + off) = *(int*)(nodeList + off);
                }
            }
        }
        DAT_80612e1c = DAT_80612e1c + 1;
    }
    FUN_80382734();
}

// ============================================================================
// fire_slot_add_collision  (0x8035b2fc)
// Register a fire slot for a collision object (marks the collision flag 8) plus
// one skeleton node. Variant of fire_slot_add used when the burning subject is
// a collision-only object.
// ============================================================================
void fire_slot_add_collision(unsigned int a1, unsigned int a2, int nodeIndex, int collisionObj)
{
    unsigned long long packed = (unsigned long long)__save_gpr_28();
    int skel = (int)packed;
    (void)a1; (void)a2;
    if ((DAT_80612e1c < 0x20) && (skel != 0) && (collisionObj != 0)) {
        int base = DAT_80612e1c * 0x74;
        int savedRt = *(int*)((int)&DAT_805e4124 + base);
        float heat = *(float*)&packed;
        memset_wrapper((int)((int)&DAT_805e40f8 + base), 0, 0x74);
        *(int*)((int)&DAT_805e4124 + base) = savedRt;
        *(int*)((int)&DAT_805e411c + base) = (int)(packed >> 0x20);
        *(float*)((int)&DAT_805e4118 + base) = heat;
        *(int*)((int)&DAT_805e4100 + base) = skel;
        *(int*)((int)&DAT_805e40f8 + base) = *(int*)(collisionObj + 0x2c);
        collision_set_flags(*(int**)(collisionObj + 4), 8);
        if ((skel != 0) && (0 < nodeIndex)) {
            int s = *(int*)((int)&DAT_805e4100 + DAT_80612e1c * 0x74) + nodeIndex * 0x2c;
            *(unsigned int*)(s + 0x58) = *(unsigned int*)(s + 0x58) | 1;
            *(int*)((int)&DAT_805e4104 + DAT_80612e1c * 0x74) = nodeIndex;
        }
        DAT_80612e1c = DAT_80612e1c + 1;
    }
    __restore_gpr_28();
}

// ============================================================================
// fire_set_flash_intensity  (0x8035b414)
// Set the shared screen-flash intensity accumulator, clamped to [0, 1].
// Called from csprop_platform_tick.
// ============================================================================
void fire_set_flash_intensity(double v)
{
    double clamped = v;
    if ((double)FLOAT_8061af28 < v) clamped = (double)FLOAT_8061af28;
    if ((double)FLOAT_8061af1c <= clamped) {
        if ((double)FLOAT_8061af28 < v) v = (double)FLOAT_8061af28;
    } else {
        v = (double)FLOAT_8061af1c;
    }
    FLOAT_80612e18 = (float)v;
}
