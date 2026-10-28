// Batch: batch_968 | Module: arcade/challenge editor drag-place entry + cursor teardown + size readout
// Address range: 0x80340cc8 - 0x80341014
// Date: 2026-09-12
//
// The arcade/challenge editor's drag-region placement entry, the placement-cursor entity teardown,
// the size-readout info box, and an inlined-out grid callback stub (continues the editor cluster from
// batches 960-967). aveditor_enter_drag_place arms the drag-rect placement mode: it resets the
// cursor, computes the letterbox fit target from the map dims, seeds the drag rect from a source
// entity, disables the viewport/input, and kicks off the setup sub-passes. aveditor_cursor_entities_destroy
// tears down the two live preview entities (+0x4150 primary, +0x4154 secondary), releasing the weather
// particle slot when the primary is a particle of kind 0x798. aveditor_draw_size_readout draws the
// numeric tile-size readout (string 0x95c) and registers the confirm soft-key (string 0x19c8).
// aveditor_grid_flood_noop is an empty callback used as a placeholder in the neighbour-flood path.
// State lives in the DAT_80612be4 editor context (flags at +0x238/+0x25c, entity slots at +0x4150/+54)
// and the DAT_80612d68 word-indexed cursor context.
// Callers: aveditor_screen_tick, aveditor_render_screen, arcade_edit_screen_enter/exit.
//
//   aveditor_enter_drag_place        0x80340cc8  arm drag-region placement: fit target, drag rect, viewport off.
//   aveditor_cursor_entities_destroy 0x80340e78  destroy the two live cursor/preview entities + weather slot.
//   aveditor_draw_size_readout       0x80340f48  draw the numeric tile-size readout + confirm soft-key.
//   aveditor_grid_flood_noop         0x80341014  empty placeholder callback (inlined out in the original).
//
// NON-MATCHING: reproduces the decompiler's structure. The int->float fit-target math uses the shared
// FLOAT_* layout globals; the >>1 / signed-round idiom for halving DAT_806109c4 is reproduced verbatim.
// FUN_8038b510 / FUN_80387fb8 (the formatted-text builder pair) and FUN_80345c2c / FUN_80345da8 (editor
// setup sub-passes) are declared file-locally with call-site-accurate arity, since the same addresses
// are used with different signatures across the tree (Compile-Clean rule 5).

#include "common.h"

extern "C" {

extern unsigned char* DAT_80612be4;   // editor context base (byte*)
extern int*  DAT_80612d68;            // cursor/camera context (word-indexed)

// map / layout dims (int cells)
extern int   DAT_806109c0;
extern int   DAT_806109c4;

// letterbox-fit layout globals
extern float FLOAT_8061abe0;
extern float FLOAT_8061abe4;
extern float FLOAT_8061abe8;
extern float FLOAT_8061abec;
extern float FLOAT_8061abf0;
extern float FLOAT_8061abf4;
extern float FLOAT_8061ab2c;
extern float FLOAT_8061ab34;
extern float FLOAT_80611a64;
extern float FLOAT_80611a68;
extern float FLOAT_80611a6c;

// weather particle-slot shared globals
extern float DAT_80561c8c;
extern float DAT_80561c98;

// formatted-text placeholder tables
extern int   DAT_80611250;
extern char  DAT_80611254;

// named cross-module editor helpers
extern void  aveditor_cursor_reset(void);
extern void  aveditor_preview_set_fit_target(int x, int y, int w, int h);
extern void  aveditor_preview_set_fit_target_ex(int x, int y, int w, int h, int tag);
extern void  aveditor_cursor_set_place_drag(int cellEntity);
extern void  aveditor_cursor_calc_world_pos(void);
extern void  aveditor_cursor_slot_store(int a, int b);
extern void  aveditor_options_screen_close(void);
extern void  hud_viewport_disable(void);
extern void  weather_slot_destroy(int* slot);
extern void  entity_destroy_with_event(void** ent);
extern int   get_string_by_id(int id);
extern void  hud_screen_register_handler(int screen, int idx, int strId, int a, int b);
extern void  text_draw_at_int_coords(void* px, void* py, void* ctx, int color);

// raw helpers used with local call-site signatures
extern int   FUN_8038b510(int* a, int* b);
extern void  FUN_80387fb8(int hdr, double d0, double d1, unsigned long long f4,
                          unsigned long long f5, unsigned long long f6, unsigned long long f7,
                          unsigned long long f8, int ctx, void* fmt, int strId,
                          unsigned int p4, unsigned int p5, unsigned int p6,
                          unsigned int p7, unsigned int p8);
extern void  FUN_80345c2c(int mode);
extern void  FUN_80345da8(int idx);

// ---------------------------------------------------------------------------
// 0x80340cc8 : arm drag-region placement mode on editor entry
// ---------------------------------------------------------------------------
void aveditor_enter_drag_place(int srcEntity)
{
    int   cellIndex = -1;
    float baseY;
    float spanY;

    *(unsigned int*)(DAT_80612be4 + 0x238) |= 0x400;
    aveditor_cursor_reset();

    if (srcEntity != 0) {
        cellIndex = (*(int*)(srcEntity + 0x2c) + 0x7fb83eb8) / 0x140;
    }

    baseY = FLOAT_8061abe0 * FLOAT_80611a64 + FLOAT_80611a6c;
    spanY = ((FLOAT_8061abe4 * FLOAT_80611a64 + FLOAT_80611a6c) - baseY) - FLOAT_8061abe8;
    aveditor_preview_set_fit_target(
        DAT_806109c4 - (((int)DAT_806109c4 >> 1) + (unsigned int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0)),
        (int)(FLOAT_8061abec + spanY * FLOAT_8061ab2c + baseY),
        DAT_806109c4, (int)spanY);

    aveditor_cursor_set_place_drag(cellIndex);
    aveditor_cursor_calc_world_pos();
    aveditor_cursor_slot_store(1, -1);

    DAT_80612d68[0x23] = srcEntity;
    DAT_80612d68[0x24] = -1;
    DAT_80612d68[0x00] = 1;
    DAT_80612d68[0x22] = -1;

    aveditor_options_screen_close();

    baseY = FLOAT_8061abe0 * FLOAT_80611a64 + FLOAT_80611a6c;
    spanY = ((FLOAT_8061abe4 * FLOAT_80611a64 + FLOAT_80611a6c) - baseY) - FLOAT_8061abe8;
    aveditor_preview_set_fit_target_ex(
        DAT_806109c4 - (((int)DAT_806109c4 >> 1) + (unsigned int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0)),
        (int)(FLOAT_8061abec + spanY * FLOAT_8061ab2c + baseY),
        DAT_806109c4, (int)spanY,
        ((int)DAT_806109c0 >> 2) + (unsigned int)((int)DAT_806109c0 < 0 && (DAT_806109c0 & 3) != 0));

    hud_viewport_disable();
    *(unsigned int*)(DAT_80612be4 + 0x25c) |= 0x2000000;

    FUN_80345c2c(6);
    FUN_80345da8(2);
    FUN_80345da8(3);
    FUN_80345da8(4);
}

// ---------------------------------------------------------------------------
// 0x80340e78 : destroy the two live cursor/preview entities
// ---------------------------------------------------------------------------
void aveditor_cursor_entities_destroy(void)
{
    int primary;

    // if the stored cursor entity equals either live slot, forget it
    if ((*(int*)(DAT_80612d68 + 0x84) == *(int*)(DAT_80612be4 + 0x4150)) ||
        (*(int*)(DAT_80612d68 + 0x84) == *(int*)(DAT_80612be4 + 0x4154))) {
        *(unsigned int*)(DAT_80612d68 + 0x84) = 0;
    }

    primary = *(int*)(DAT_80612be4 + 0x4150);
    if (primary != 0) {
        if ((*(short*)(primary + 0x94) == 0x798) && (*(int**)(primary + 0x7c) != (int*)0)) {
            weather_slot_destroy(*(int**)(primary + 0x7c));
            DAT_80561c8c = FLOAT_8061ab34;
            *(unsigned int*)(*(int*)(DAT_80612be4 + 0x4150) + 0x7c) = 0;
            DAT_80561c98 = DAT_80561c8c;
        }
        entity_destroy_with_event(*(void***)(DAT_80612be4 + 0x4150));
        *(unsigned int*)(DAT_80612be4 + 0x4150) = 0;
    }

    if (*(void***)(DAT_80612be4 + 0x4154) != (void**)0) {
        entity_destroy_with_event(*(void***)(DAT_80612be4 + 0x4154));
        *(unsigned int*)(DAT_80612be4 + 0x4154) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x80340f48 : draw the numeric tile-size readout + confirm soft-key
// ---------------------------------------------------------------------------
void aveditor_draw_size_readout(int screen, unsigned int p2, unsigned int p3, unsigned int p4,
                                unsigned int p5, unsigned int p6, unsigned int p7, unsigned int p8)
{
    unsigned char* ctx = DAT_80612be4;
    double         sizeX;
    double         sizeXd;
    unsigned int   coordX;
    unsigned int   coordY;
    int            hdr;
    int            strId;

    sizeX  = (double)FLOAT_8061abf0;
    sizeXd = (double)(float)(sizeX * (double)FLOAT_80611a68);
    coordX = (unsigned int)(sizeX * (double)FLOAT_80611a68);
    coordY = (unsigned int)(FLOAT_8061abf4 * FLOAT_80611a64);

    hdr   = FUN_8038b510((int*)DAT_80612be4, (int*)&DAT_80611250);
    strId = get_string_by_id(0x95c);
    FUN_80387fb8(hdr, sizeXd, sizeX, 0, 0, 0, 0, 0, (int)ctx, &DAT_80611254, strId,
                 p4, p5, p6, p7, p8);

    text_draw_at_int_coords(&coordX, &coordY, ctx, 0x64646480);

    strId = get_string_by_id(0x19c8);
    hud_screen_register_handler(screen, 1, strId, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x80341014 : empty placeholder callback (inlined out in the original)
// ---------------------------------------------------------------------------
void aveditor_grid_flood_noop(void)
{
}

} // extern "C"
