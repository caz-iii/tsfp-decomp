// Address: 0x80357a58 | Name: weather_flare_render_composite | Date: 2026-09-12
// Address: 0x80357bf0 | Name: weather_flare_spawn_configured | Date: 2026-09-12
// Address: 0x80357c5c | Name: weather_flare_autofade_check   | Date: 2026-09-12
// Address: 0x80357cc0 | Name: weather_flare_stub_nop         | Date: 2026-09-12
//
// Weather flare composite render pass and flare-slot spawn/lifetime helpers.
// weather_flare_render_composite is invoked from session_render_scene_by_mode:
// it renders the scene with render-suppression active (so tracked entities are
// hidden), runs the lightning/flare pass, then re-renders the suppressed and
// forced entities into two material buffers so the flare glow composites over
// them correctly. The spawn helper reads the per-type flare config table
// (DAT_804873xx, 0x2c-byte stride).

#include "common.h"

// ---- globals ---------------------------------------------------------------
extern int DAT_80612dfc;        // suppress-active flag (shared)
extern int DAT_80612df8;        // tracked-entity count
extern int DAT_80612df0;        // tracked entity handle array base
extern int DAT_80612de0;        // forced-visible count
extern int DAT_80612dd8;        // forced-visible pointer array base
extern int DAT_80611c24;        // entity table base (0x2e8-stride)
extern int DAT_80611748;        // viewport/render ctx (int[])
extern unsigned int DAT_805015c0; // global mode flags

// flare config table (0x2c stride)
extern int DAT_804873e0;        // +0x00 type/id
extern int DAT_804873f4;        // +0x14 param a (float)
extern int DAT_804873f8;        // +0x18 color id
extern int DAT_804873fc;        // +0x1c activate time (float)
extern int DAT_80487400;        // +0x20 min-life threshold (float)
extern int DAT_80487404;        // +0x24 fade time (float)
extern int DAT_80487408;        // +0x28 param b (float)

extern float FLOAT_8061adf4;    // 0.0

// ---- helpers ---------------------------------------------------------------
extern unsigned long long FUN_803826e4(void);
extern void FUN_80382730(void);
extern int  FUN_803826f0(void);
extern void FUN_8038273c(void);
extern int  gx_get_blend_mode(void);
extern void gx_set_blend_mode(int mode);
extern void gx_set_color_update_mode(void);
extern void gx_set_sub_material_id(int id);
extern void gx_fill_rect_flags(int a, int x0, int y0, int x1, int y1, int f);

extern void scene_visibility_and_render(unsigned long long ctx, double a, unsigned long long b,
                                        unsigned long long c, unsigned long long d,
                                        unsigned long long e, unsigned long long f,
                                        unsigned long long g);
extern void weather_lightning_render_flare_pass(void);
extern unsigned long long entity_render_single(int entity, int flags);
extern unsigned long long cutscene_render_slot_callback(
        unsigned long long ctx, double a, unsigned long long b, unsigned long long c,
        unsigned long long d, unsigned long long e, unsigned long long f, unsigned long long g,
        int slot, int z, unsigned int p11, unsigned int p12, unsigned int p13,
        unsigned int p14, unsigned int p15, unsigned int p16);

// flare pool (returns the slot pointer at the call site; defined void elsewhere)
extern int  weather_flare_pool_add(double a, double b, int c, int d, int e);
extern void weather_flare_activate(double startTime, int slot);
extern void weather_flare_begin_fade(double startTime, int slot);

// ============================================================================
// weather_flare_render_composite  (0x80357a58)
// ============================================================================
void weather_flare_render_composite(double param_2, unsigned long long param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_11,
                                    unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15,
                                    unsigned int param_16)
{
    unsigned long long ctx = FUN_803826e4();
    int savedBlend = gx_get_blend_mode();

    DAT_80612dfc = 1;
    scene_visibility_and_render(ctx, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    DAT_80612dfc = 0;

    weather_lightning_render_flare_pass();
    gx_set_blend_mode(4);
    gx_set_color_update_mode();
    gx_set_sub_material_id(0);

    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            gx_set_sub_material_id(0x1c);
            gx_set_color_update_mode();
        } else {
            gx_set_sub_material_id(6);
            gx_set_color_update_mode();
        }

        int off = 0;
        for (int i = 0; i < DAT_80612df8; i++) {
            unsigned int handle = *(unsigned int*)(&DAT_80612df0 + off);
            int ent;
            if (handle == 0) {
                ent = 0;
            } else {
                ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
                if (*(unsigned int*)(ent + 0x2c) != handle) {
                    ent = 0;
                    *(int*)(&DAT_80612df0 + off) = 0;
                }
            }
            if (ent != 0) {
                entity_render_single(ent, 0);
            }
            off += 4;
        }

        if (((DAT_805015c0 & 0x20000000) != 0) || ((DAT_805015c0 & 0x200) != 0)) {
            int foff = 0;
            for (int i = 0; i < DAT_80612de0; i++) {
                cutscene_render_slot_callback(ctx, param_2, param_3, param_4, param_5, param_6,
                                              param_7, param_8, *(int*)(&DAT_80612dd8 + foff), 0,
                                              param_11, param_12, param_13, param_14, param_15, param_16);
                foff += 4;
            }
        }
    }

    gx_set_sub_material_id(0);
    gx_fill_rect_flags(8, *(int*)DAT_80611748, *(int*)(DAT_80611748 + 0x14),
                       *(int*)(DAT_80611748 + 4) + 1, *(int*)(DAT_80611748 + 0x18) + 1, 0);
    gx_set_blend_mode(savedBlend);
    FUN_80382730();
}

// ============================================================================
// weather_flare_spawn_configured  (0x80357bf0)
// Spawn and activate a flare slot from the current weather type's flare config
// row (index from the frame ctx), stashing the config index at slot +0x1d8.
// ============================================================================
void weather_flare_spawn_configured(void)
{
    int cfgIdx = FUN_803826f0();
    int row = cfgIdx * 0x2c;
    int slot = weather_flare_pool_add(
        (double)*(float*)(&DAT_804873f4 + row),
        (double)*(float*)(&DAT_80487408 + row),
        *(int*)(&DAT_804873e0 + row),
        row - 0x7fb78c18,
        *(int*)(&DAT_804873f8 + row));
    *(int*)(slot + 0x1d8) = cfgIdx;
    weather_flare_activate((double)*(float*)(&DAT_804873fc + row), slot);
    FUN_8038273c();
}

// ============================================================================
// weather_flare_autofade_check  (0x80357c5c)
// When the flare is in hold state 2 and its elapsed time has passed the config
// min-life threshold, kick off the fade-out.
// ============================================================================
void weather_flare_autofade_check(int slot)
{
    int row = *(int*)(slot + 0x1d8) * 0x2c;
    if ((FLOAT_8061adf4 < *(float*)(&DAT_80487400 + row)) &&
        (*(int*)(slot + 4) == 2) &&
        (*(float*)(&DAT_80487400 + row) <= *(float*)(slot + 8))) {
        weather_flare_begin_fade((double)*(float*)(&DAT_80487404 + row), slot);
    }
}

// ============================================================================
// weather_flare_stub_nop  (0x80357cc0)
// Empty placeholder callback.
// ============================================================================
void weather_flare_stub_nop(void)
{
}
