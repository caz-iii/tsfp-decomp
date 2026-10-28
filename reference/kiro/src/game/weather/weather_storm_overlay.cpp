// Address: 0x803585f0 | Name: weather_storm_sound_tick        | Date: 2026-09-12
// Address: 0x80358634 | Name: weather_storm_render_overlay    | Date: 2026-09-12
// Address: 0x80358c48 | Name: weather_electric_effect_init    | Date: 2026-09-12
//
// Storm ambience overlay + electric-effect particle-def setup.
//   weather_storm_sound_tick   updates the looping 3D storm/thunder ambience
//                              source (sound 0xc95) each frame from the current
//                              storm intensity, called from the per-mode frame
//                              update (session_frame_update_by_mode).
//   weather_storm_render_overlay  draws the full-screen rain-streak / lightning
//                              flash overlay during the post-HUD render pass
//                              (session_render_post_hud), gated by the storm
//                              gameplay-modifier bit 0x2000. It fills the
//                              viewport quad with a distortion material (sprite
//                              0x295) plus a jittered vertical lightning streak.
//   weather_electric_effect_init  one-time setup of the four shared electric-arc
//                              particle-def / emitter templates used by the
//                              melee electro-hit effect, called from
//                              weather_system_reset.

#include "common.h"

// ---- storm sound state -----------------------------------------------------
extern float FLOAT_80612e0c;   // shared storm/near-plane state scalar (prev intensity)
extern int   DAT_80612e08;     // storm sound source handle base
extern float FLOAT_8061ae90;   // sound param a
extern float FLOAT_8061ae74;   // sound param b (shared)
extern float FLOAT_8061ae94;   // target storm intensity

// ---- render-overlay globals ------------------------------------------------
extern unsigned int DAT_806125fc;   // gameplay-modifier mask a
extern unsigned int DAT_80612600;   // gameplay-modifier mask b
extern int          DAT_80612614;   // current level id
extern int          DAT_805cc614;   // level sub-state
extern int          DAT_80611748;   // viewport/render ctx (int[])
extern int          DAT_80610a20;   // sprite/material table ctx (int[])
extern int          DAT_80612254;   // bound material ptr
extern unsigned int DAT_806109c0;   // rng modulus / denom
extern unsigned int DAT_806109c8;   // screen height
extern float        FLOAT_80611280; // persistent lightning-streak x

// ---- float / double constants ----------------------------------------------
extern float  FLOAT_8061ae98;
extern double DOUBLE_8061ae88;
extern float  FLOAT_8061ae84;
extern double DOUBLE_8061ae78;
extern float  FLOAT_8061aeb0;
extern float  FLOAT_806108ec;
extern float  FLOAT_8061aeb4;
extern double DOUBLE_8061aea0;
extern double DOUBLE_8061aea8;

// ---- electric-effect particle-def templates --------------------------------
extern int   DAT_805e3bb8;   // arc particle-def 0
extern int   DAT_805e3c84;   // arc emitter 0
extern int   DAT_805e3d08;   // arc particle-def 1
extern int   DAT_805e3dd4;   // arc emitter 1
extern int   DAT_805e3e58;   // arc particle-def 2 (copy of def 1)
extern int   DAT_805e3f24;   // arc emitter 2 (copy of emitter 1)
extern int   DAT_805e3fa8;   // arc particle-def 3
extern int   DAT_805e4074;   // arc trail emitter
extern float DAT_805e4088;   // trail tuning x
extern float DAT_805e408c;   // trail tuning y
extern float DAT_805e4090;   // trail tuning z

extern float FLOAT_8061aeb8, FLOAT_8061aebc, FLOAT_8061aec0, FLOAT_8061aec4, FLOAT_8061aec8;
extern float FLOAT_8061aecc, FLOAT_8061aed0, FLOAT_8061aed4, FLOAT_8061aed8, FLOAT_8061aedc;
extern float FLOAT_8061aee0, FLOAT_8061aee4, FLOAT_8061aee8, FLOAT_8061aeec, FLOAT_8061aef0;
extern float FLOAT_8061aef4, FLOAT_8061aef8, FLOAT_8061aefc, FLOAT_8061af00;

// ---- helpers ---------------------------------------------------------------
extern void sound_update_3d_source(double a, double b, double c, void* src, int d, int soundId, int e);
extern int  gameplay_modifier_init_all(void);
extern int  rng_counter_tick(void);
extern int  sprite_get_texture_id(int ref);
extern void gx_set_sub_material_id(int id);
extern void gx_bind_material_fallback(void);
extern void gx_fill_rect_solid(double a, double b, double c, double d,
                               unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
extern int  gx_set_material_override_cb(int cb);
extern void gx_tev_distort_teardown(void);
extern void gx_draw_line_i(int x0, int y0, int x1, int y1, int color);
extern void FUN_803826dc(void);   // render scope push
extern void FUN_80382728(void);   // render scope pop
extern void FUN_803826dc(void);

// particle-def / emitter setup helpers (arities matched to call sites)
extern void particle_def_create(double a, double b, void* def, int c, int d, int e, int ref, int f);
extern void particle_def_set_size_keyframes(double a, double b, double c, double d,
                                            int k0, int k1, int k2);
extern void particle_def_set_velocity_ranges(double a, double b, double c, double d, double e, double f);
extern void particle_def_set_size_start_end(double a, double b, double c, double d);
extern void particle_def_set_color_end(double a, double b, double c, double d);
extern void particle_def_copy(int dst, int src);
extern void particle_def_set_blend_mode(int ref);
extern void particle_emitter_create(double a, double b, double c, void* emitter, void* def, int d, int e);
extern void particle_emitter_set_velocity_range(double a, double b);
extern void particle_emitter_set_color_range(double a, double b, double c, double d);
extern void particle_emitter_copy(int dst, void* srcEmitter, int srcEmitter2);

// ============================================================================
// weather_storm_sound_tick  (0x803585f0)
// Update the looping 3D storm ambience source each frame from the current storm
// intensity, then latch the new intensity as the previous value.
// ============================================================================
void weather_storm_sound_tick(void)
{
    sound_update_3d_source((double)FLOAT_80612e0c, (double)FLOAT_8061ae90, (double)FLOAT_8061ae74,
                           &DAT_80612e08, 0, 0xc95, 0);
    FLOAT_80612e0c = FLOAT_8061ae94;
}

// ============================================================================
// weather_storm_render_overlay  (0x80358634)
// Full-screen storm overlay: when the storm gameplay-modifier bit 0x2000 is set
// (and the level allows it) fill the viewport quad with the distortion material
// (sprite 0x295) as a set of eight edge/corner rects, apply a distort TEV pass,
// and draw a jittered vertical lightning streak. Wrapped in a render scope.
// ============================================================================
void weather_storm_render_overlay(void)
{
    FUN_803826dc();
    unsigned int mod = (unsigned int)gameplay_modifier_init_all();
    if (((DAT_806125fc & DAT_80612600 & mod & 0x2000) != 0) &&
        ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
        int* vp = (int*)DAT_80611748;
        unsigned int left  = (unsigned int)vp[0];
        unsigned int top    = (unsigned int)vp[1] + 1;
        unsigned int bottom = (unsigned int)vp[5];
        unsigned int rightE = (unsigned int)vp[6] + 1;

        int tid = sprite_get_texture_id(0x295);
        unsigned int marginX = (unsigned int)*(unsigned short*)(*(int*)DAT_80610a20 + tid * 0x58 + 0x4e);
        tid = sprite_get_texture_id(0x295);
        unsigned int marginY = (unsigned int)*(unsigned short*)(*(int*)DAT_80610a20 + tid * 0x58 + 0x52);

        int r = rng_counter_tick();
        double phase = (double)(FLOAT_8061ae98 *
            ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae88) / FLOAT_8061ae84));

        gx_set_sub_material_id(1);
        tid = sprite_get_texture_id(0x295);
        int* stab = (int*)DAT_80610a20;
        if ((tid < 0) || (stab[3] <= tid) || (tid <= stab[2])) {
            gx_bind_material_fallback();
        } else {
            DAT_80612254 = *(int*)DAT_80610a20 + tid * 0x58;
        }

        unsigned int rInner = left + marginX;
        unsigned int bInner = bottom + marginY;
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           left, bottom, rInner, bInner);
        unsigned int tInner = top - marginY;
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           left, bInner, rInner, tInner);
        unsigned int lInner = top - marginX;
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           top, bottom, lInner, bInner);
        gx_fill_rect_solid(DOUBLE_8061aea8, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           rInner, bottom, lInner, bInner);
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           left, top, rInner, tInner);
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           top, bInner, lInner, tInner);
        gx_fill_rect_solid(DOUBLE_8061aea0, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           top, top, lInner, tInner);
        gx_fill_rect_solid(DOUBLE_8061aea8, DOUBLE_8061aea0, DOUBLE_8061aea8, DOUBLE_8061aea8,
                           rInner, top, lInner, tInner);

        int savedCb = gx_set_material_override_cb(-0x7fe7159c);
        float sTop = (float)((double)CONCAT44(0x43300000, bottom ^ 0x80000000) - DOUBLE_8061ae78) /
                     (float)((double)CONCAT44(0x43300000, (unsigned int)stab[6] ^ 0x80000000) - DOUBLE_8061ae78);
        float sBot = (float)((double)CONCAT44(0x43300000, top ^ 0x80000000) - DOUBLE_8061ae78) /
                     (float)((double)CONCAT44(0x43300000, (unsigned int)stab[6] ^ 0x80000000) - DOUBLE_8061ae78);
        gx_fill_rect_solid(DOUBLE_8061aea0,
                           (double)(float)(phase + (double)sTop), DOUBLE_8061aea8,
                           (double)(float)(phase + (double)sBot),
                           left, bottom, top, top);
        gx_tev_distort_teardown();
        gx_set_material_override_cb(savedCb);

        r = rng_counter_tick();
        float leftF = (float)((double)CONCAT44(0x43300000, left ^ 0x80000000) - DOUBLE_8061ae78);
        FLOAT_80611280 = FLOAT_8061aeb0 * FLOAT_806108ec *
            (FLOAT_8061ae74 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061ae78)) *
            ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae88) / FLOAT_8061ae84 - FLOAT_8061aeb4) +
            FLOAT_80611280;
        float rightF = (float)((double)CONCAT44(0x43300000, top ^ 0x80000000) - DOUBLE_8061ae78);
        if ((FLOAT_80611280 < leftF) || (rightF < FLOAT_80611280) ||
            (((unsigned int)(r = rng_counter_tick()) -
              ((unsigned int)r / DAT_806109c0) * DAT_806109c0) * 5 == 0)) {
            r = rng_counter_tick();
            FLOAT_80611280 =
                (float)((double)CONCAT44(0x43300000, (top - left) ^ 0x80000000) - DOUBLE_8061ae78) *
                ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061ae88) / FLOAT_8061ae84) +
                (float)((double)CONCAT44(0x43300000, left ^ 0x80000000) - DOUBLE_8061ae78);
        }
        gx_draw_line_i((int)FLOAT_80611280, bottom, (int)FLOAT_80611280, top, 0x2f);
    }
    FUN_80382728();
}

// ============================================================================
// weather_electric_effect_init  (0x80358c48)
// One-time construction of the four shared electric-arc particle-def / emitter
// templates (defs 805e3bb8 / 805e3d08 / 805e3e58 / 805e3fa8 with emitters
// 805e3c84 / 805e3dd4 / 805e3f24 / 805e4074), plus the three trail tuning
// scalars at 805e4088. Called once from weather_system_reset.
// ============================================================================
void weather_electric_effect_init(void)
{
    double c;

    particle_def_create((double)FLOAT_8061aeb8, (double)FLOAT_8061aebc, &DAT_805e3bb8, 0, 0, 3, 0x3bd, 0);
    particle_def_set_size_keyframes((double)FLOAT_8061aebc, (double)FLOAT_8061aec0,
                                    (double)FLOAT_8061aec4, (double)FLOAT_8061aec4,
                                    0x28282800, 0x1e1e1e00, 0x14141400);
    particle_def_set_velocity_ranges((double)FLOAT_8061aec4, (double)FLOAT_8061aec0,
                                     (double)FLOAT_8061aeb8, (double)FLOAT_8061aec0,
                                     (double)FLOAT_8061aebc, (double)FLOAT_8061aebc);
    particle_def_set_size_start_end((double)FLOAT_8061aec0, (double)FLOAT_8061aebc,
                                    (double)FLOAT_8061aec0, (double)FLOAT_8061aebc);
    c = (double)FLOAT_8061aec0;
    particle_def_set_color_end(c, c, (double)FLOAT_8061aec8, c);
    particle_emitter_create((double)FLOAT_8061aecc, (double)FLOAT_8061aed0, (double)FLOAT_8061aed4,
                            &DAT_805e3c84, &DAT_805e3bb8, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_8061aebc, (double)FLOAT_8061aec0);
    particle_emitter_set_color_range((double)FLOAT_8061aed8, (double)FLOAT_8061aec0,
                                     (double)FLOAT_8061aedc, (double)FLOAT_8061aee0);

    particle_def_create((double)FLOAT_8061aebc, (double)FLOAT_8061aebc, &DAT_805e3d08, 0, 0, 3, 0x3bd, 0);
    particle_def_set_size_keyframes((double)FLOAT_8061aebc, (double)FLOAT_8061aec0,
                                    (double)FLOAT_8061aec4, (double)FLOAT_8061aec4,
                                    0x3c282800, 0x1e1e1e00, 0x14141400);
    particle_def_set_velocity_ranges((double)FLOAT_8061aed4, (double)FLOAT_8061aeb8,
                                     (double)FLOAT_8061aee4, (double)FLOAT_8061aed4,
                                     (double)FLOAT_8061aebc, (double)FLOAT_8061aebc);
    particle_def_set_size_start_end((double)FLOAT_8061aec0, (double)FLOAT_8061aebc,
                                    (double)FLOAT_8061aec0, (double)FLOAT_8061aebc);
    c = (double)FLOAT_8061aec0;
    particle_def_set_color_end(c, c, (double)FLOAT_8061aecc, c);
    particle_emitter_create((double)FLOAT_8061aecc, (double)FLOAT_8061aee8, (double)FLOAT_8061aed4,
                            &DAT_805e3dd4, &DAT_805e3d08, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_8061aebc, (double)FLOAT_8061aec0);
    c = (double)FLOAT_8061aec0;
    particle_emitter_set_color_range(c, c, (double)FLOAT_8061aeec, c);

    particle_def_copy(0x805e3e58, 0x805e3d08);
    particle_def_set_blend_mode(0x3be);
    particle_def_set_size_keyframes((double)FLOAT_8061aebc, (double)FLOAT_8061aec0,
                                    (double)FLOAT_8061aec4, (double)FLOAT_8061aec4,
                                    0x0a0a0a00, 0, 0x14141400);
    particle_emitter_copy(0x805e3f24, &DAT_805e3e58, 0x805e3dd4);

    particle_def_create((double)FLOAT_8061aef0, (double)FLOAT_8061aebc, &DAT_805e3fa8, 0, 0, 3, 0x3bd, 0);
    particle_def_set_size_keyframes((double)FLOAT_8061aebc, (double)FLOAT_8061aec0,
                                    (double)FLOAT_8061aec4, (double)FLOAT_8061aec4,
                                    0x50282800, 0x1e1e1e00, 0x14141400);
    c = (double)FLOAT_8061aec4;
    particle_def_set_velocity_ranges(c, c, c, (double)FLOAT_8061aed8,
                                     (double)FLOAT_8061aebc, (double)FLOAT_8061aebc);
    particle_def_set_size_start_end((double)FLOAT_8061aec0, (double)FLOAT_8061aebc,
                                    (double)FLOAT_8061aec0, (double)FLOAT_8061aebc);
    particle_def_set_color_end((double)FLOAT_8061aec4, (double)FLOAT_8061aec0,
                               (double)FLOAT_8061aef4, (double)FLOAT_8061aec0);
    particle_emitter_create((double)FLOAT_8061aecc, (double)FLOAT_8061aecc, (double)FLOAT_8061aed8,
                            &DAT_805e4074, &DAT_805e3fa8, 0, 1);
    particle_emitter_set_velocity_range((double)FLOAT_8061aebc, (double)FLOAT_8061aed8);
    particle_emitter_set_color_range((double)FLOAT_8061aef8, (double)FLOAT_8061aec0,
                                     (double)FLOAT_8061aefc, (double)FLOAT_8061af00);

    DAT_805e4088 = FLOAT_8061aebc;
    DAT_805e408c = FLOAT_8061aec0;
    DAT_805e4090 = FLOAT_8061aec0;
}
