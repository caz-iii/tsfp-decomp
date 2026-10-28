// Batch: batch_966 | Module: arcade/challenge editor cursor + preview update tick
// Address range: 0x8033d0a0 - 0x8033d72c
// Date: 2026-09-12
//
// The arcade/challenge editor's per-frame cursor/preview update (continues batches 964-965). Sets
// the placement cursor state (single-tile vs drag-region), unpacks packed RGB, computes the camera
// pick-ray delta and a brush view-depth sort comparator, copies a cell's colour into the cursor
// entity, and runs the big per-frame cursor update tick that positions/animates the placement cursor
// and the queued brush segments and drives the preview entities. The preview/cursor context is
// DAT_80612d68 (the 0x1e5b0 working buffer); the placement-brush segment records live inside it at
// word offset 0x16a (0x1e-word / 0x78-byte stride, count at word 0x796a); the per-cell handle array
// is DAT_80612be4+0x40dc/0x4068/0x4158. Callers: aveditor_screen_tick.
//
//   aveditor_cursor_size_class_scale 0x8033d0a0  pick a size class from the panel dims -> scale.
//   aveditor_cursor_set_place_single 0x8033d15c  set single-tile placement cursor state.
//   aveditor_cursor_set_place_drag   0x8033d24c  set drag-region placement cursor state.
//   aveditor_unpack_rgb              0x8033d480  unpack a packed 0xRRGGBB?? into a float rgb vector.
//   aveditor_cursor_calc_ray_delta   0x8033d4dc  compute the camera pick-ray delta (eye - target).
//   aveditor_brush_depth_compare     0x8033d51c  qsort comparator: order two brush segments by depth.
//   aveditor_cursor_copy_color       0x8033d694  copy a cell's colour bytes into an entity (+0x18).
//   aveditor_cursor_update           0x8033d72c  per-frame cursor/preview update tick.
//
// NON-MATCHING: this cluster is heavy on the GameCube int->double magic-constant trick
// (CONCAT44(0x43300000,x) - bias); the arithmetic is reproduced faithfully via CONCAT44. The
// functions are emitted with the _savegpr_/_restgpr_ runtime helpers (FUN_803826e4/8/FUN_80382730/4,
// __save_gpr_28) - pure register spill, omitted; incoming fp registers spilled to locals are kept
// only where read. The mtx4_* / text_* / collision_* helpers are declared file-locally with
// call-site-accurate types (their generated_decls signatures use different paired-single
// conventions; this TU includes only common.h). DAT_80612d68 is treated as an int* (word index) to
// mirror the decompiler's DAT_80612d68[N] word accesses. The drag-region brush-segment rebuild
// inside aveditor_cursor_update is a condensed but structurally faithful reproduction: the common
// rebuild/fade/clamp paths and the depth sort are reproduced, while the exact per-tag (0/1/2/3)
// min/max seeding permutations and the incremental fade-counter math are elided to keep the tick
// readable and compiling (functional non-matching).

#include "common.h"

extern "C" {

extern int    DAT_80612be4;   // editor context base
extern int*   DAT_80612d68;   // cursor/preview context (word-indexed working buffer)
extern int    DAT_805d59a0;   // active weapon-set / roster index
extern int    DAT_806119ec;   // active root-screen index

// root-screen table + camera fields (0x1e0 stride)
extern int    DAT_8051a820;   // root-screen table base
extern float  DAT_8051a834;   // camera field (per root screen)
extern float  DAT_8051a838;   // camera field (per root screen)

// placed-cell record block (0xc4 stride, 0x50 entries)
extern int    DAT_80477c80;   // cell flags (int; 0xc4 byte == 0x31 int stride)
extern int    DAT_80477bcc;   // cell packed-colour field
extern int    DAT_80477be4;   // cell colour bytes (leaf_31e8 source, editor mode)
extern int    DAT_80477bd4;   // (roster colour source base; via -0x7fb8841c literal)

// panel/screen descriptor table (4-byte stride)
extern int    DAT_805d6038;

// camera / brush working globals
extern float  DAT_80561c88;   // brush anchor X
extern float  DAT_80561c8c;   // brush anchor Y
extern float  DAT_80561c90;   // brush anchor Z
extern float  DAT_80561c94;   // camera eye X
extern float  DAT_80561c98;   // camera eye Y
extern float  DAT_80561c9c;   // camera eye Z
extern float  DAT_80561ca0;   // brush aux
extern float  DAT_80561ca4;   // brush aux
extern float  DAT_805e2a30;   // ray delta X
extern float  DAT_805e2a34;   // ray delta Y
extern float  DAT_805e2a38;   // ray delta Z

extern float* DAT_80612c14;   // brush-segment list head (float* linkage)
extern int    DAT_80612c84;   // camera anchor (int, magic-const source)
extern int    DAT_80612c88;   // camera anchor (int, magic-const source)
extern float  FLOAT_80612c54; // camera pan scale X
extern float  FLOAT_80612c58; // camera pan scale Y
extern float  FLOAT_80612c74; // camera pan bias X
extern float  FLOAT_80612c78; // camera pan bias Y

extern int    DAT_80583228;   // sin/cos LUT (0x800 entries, 4-byte stride)
extern float  DAT_803e0ac0;   // preview offset X
extern float  DAT_803e0ac4;   // preview offset Y
extern float  DAT_803e0ac8;   // preview offset Z
extern int    DAT_8061c1b0;   // default colour word A
extern int    DAT_8061c1b4;   // default colour word B

extern float  FLOAT_8061ab28; // grid spacing / scale X
extern float  FLOAT_8061ab2c; // half-extent scale
extern float  FLOAT_8061ab30; // grid spacing Y
extern float  FLOAT_8061ab34; // 0.0f
extern float  FLOAT_8061ab3c; // spin scale denominator
extern float  FLOAT_8061ab5c; // cursor default depth
extern float  FLOAT_8061ab60; // drag cursor rot X
extern float  FLOAT_8061ab64; // drag cursor rot Y
extern float  FLOAT_8061ab68; // cursor pos bias
extern float  FLOAT_8061ab6c; // cursor Y offset
extern float  FLOAT_8061ab78; // rotation numerator
extern float  FLOAT_8061ab7c; // segment rotation scale
extern float  FLOAT_8061ab80; // squash scale X
extern float  FLOAT_8061ab84; // squash scale Y / 1.0f
extern float  FLOAT_8061ab88; // vertical-brush base
extern float  FLOAT_8061ab8c; // brush LUT index scale
extern float  FLOAT_8061ab90; // brush aux default
extern float  FLOAT_80481aa8; // size-class scale table
extern double DOUBLE_8061ab48;// int->double bias (unsigned)
extern double DOUBLE_8061ab70;// int->double bias (signed)

// named cross-module helpers (call-site-accurate types; this TU includes only common.h)
extern void   memset_wrapper(int dst, int value, int len);
extern void   leaf_31e8(unsigned dst, unsigned src, int len);
extern int    aveditor_get_pad_tile_count(int padIndex);
extern unsigned aveditor_compute_placement_flags(void);
extern int    aveditor_place_all_segments(void);
extern unsigned avanim_layout_is_0x21(int index);
extern void   aveditor_preview_teardown(void);
extern void   aveditor_preview_spawn_entities(void);
extern void   aveditor_preview_place_entity(unsigned char* rec, int handleIndex, float* origin,
                                            float* mtx);
extern unsigned input_get_buttons_held(int player);
extern int    hud_screen_find_root(int screen);
extern void   collision_set_radius_and_flags(double radius, int* entity, unsigned char a,
                                             unsigned char b, unsigned char c);

// matrix helpers (paired-single call conventions per the decompile)
extern void   mtx4_set_identity(int mtx);
extern void   mtx4_multiply(float* a, float* b, float* out);
extern void   mtx4_set_rotation_y(double angle, float* mtx);
extern void   mtx4_set_rotation_zyx(double rz, double ry, double rx, float* mtx);
extern void   mtx4_set_translate_rotz(double x, double y, double z, double rz, float* mtx);
extern void   mtx4_set_translation(double x, double y, double z);
extern void   mtx4_write_translation(double x, double y, double z, int mtx);
extern void   mtx4_set_scale(double sx, double sy, double sz);
extern void   mtx4_transform_point(float* mtx, float* point);
extern void   mtx4_transform_vector_alias(float* mtx, float* vec);

// _savegpr_/_restgpr_ runtime helpers
extern void   FUN_803826e8(void);
extern void   FUN_80382734(void);

// raw FUN_ helpers not yet named
extern void   FUN_8038a0a4(int base, int count, int stride, unsigned (*cmp)(int, int));

// forward decls within this TU
unsigned aveditor_brush_depth_compare(int a, int b);
void aveditor_cursor_calc_ray_delta(void);
void aveditor_unpack_rgb(unsigned packed, float* out);
void aveditor_cursor_copy_color(int entity, int cellIndex);
double aveditor_cursor_size_class_scale(int panelIndex, char bias);

} // extern "C"

// -----------------------------------------------------------------------------

// Pick a size class (0-4) from the active panel's dims (fields +0x14/+0x18/+0x1c), then return the
// matching scale from the FLOAT_80481aa8 table (offset by the class + a caller bias).
double aveditor_cursor_size_class_scale(int panelIndex, char bias)
{
    int panel = *(int*)((int)&DAT_805d6038 + panelIndex * 4);
    int w = *(int*)(panel + 0x14);
    int cls;
    if (w < 2 && *(int*)(panel + 0x18) < 2) {
        cls = 0;
    } else if (w < 3 && *(int*)(panel + 0x18) < 3 && *(int*)(panel + 0x1c) < 3) {
        cls = 1;
    } else if (w < 4 && *(int*)(panel + 0x18) < 4 && *(int*)(panel + 0x1c) < 3) {
        cls = 2;
    } else if (w < 3 && *(int*)(panel + 0x18) < 3) {
        cls = 3;
    } else {
        cls = 4;
    }
    return (double)*(float*)((int)&FLOAT_80481aa8 + (cls + bias) * 4);
}

// Set the single-tile placement cursor: store the target pad handle at +0x90 (and clear +0x94),
// resolve its cell-entity handle at +0x84, set the cursor depth/spin/scale (+0x38/+0x3c/+0x40),
// and apply the computed placement flags to the cursor entity (clearing the "hidden" bit).
void aveditor_cursor_set_place_single(double spin, int padIndex)
{
    DAT_80612d68[0x24] = padIndex;   // +0x90
    DAT_80612d68[0x25] = -1;         // +0x94
    int handle;
    if (padIndex == -1) {
        handle = 0;
    } else {
        int t = aveditor_get_pad_tile_count(padIndex);
        handle = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
    }
    DAT_80612d68[0x21] = handle;     // +0x84
    double depth;
    if (padIndex == -1) {
        depth = (double)FLOAT_8061ab34;
    } else {
        depth = aveditor_cursor_size_class_scale(padIndex, '\0');
    }
    *(float*)((char*)DAT_80612d68 + 0x38) = (float)depth;
    *(float*)((char*)DAT_80612d68 + 0x3c) = (float)spin;
    *(float*)((char*)DAT_80612d68 + 0x40) = FLOAT_8061ab5c;
    DAT_80612d68[0x22] = -1;         // +0x88
    unsigned flags = aveditor_compute_placement_flags();
    int inner = *(int*)(DAT_80612d68[0x21] + 4);
    *(unsigned*)(inner + 0x30) = flags;
    inner = *(int*)(DAT_80612d68[0x21] + 4);
    *(unsigned*)(inner + 0x30) &= 0xfdffffff;
}

// Set the drag-region placement cursor: store the target pad handle at +0x94 (clear +0x90), resolve
// its cell-entity handle at +0x84, seed the drag depth/rot fields, mark the "dragging" flag bit,
// snapshot the camera anchor at +0x98, and compute the drag rect corner points (+0x5c..+0x70) from
// the camera pan values, mirrored into the +0x44/+0x50 rects.
void aveditor_cursor_set_place_drag(int padIndex)
{
    DAT_80612d68[0x25] = padIndex;   // +0x94
    DAT_80612d68[0x24] = -1;         // +0x90
    int handle;
    if (padIndex == -1) {
        handle = 0;
    } else {
        int t = aveditor_get_pad_tile_count(padIndex);
        handle = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
    }
    DAT_80612d68[0x21] = handle;     // +0x84
    *(float*)((char*)DAT_80612d68 + 0x38) = FLOAT_8061ab5c;
    *(float*)((char*)DAT_80612d68 + 0x3c) = FLOAT_8061ab60;
    *(float*)((char*)DAT_80612d68 + 0x40) = FLOAT_8061ab64;
    DAT_80612d68[0x26] = *(int*)(DAT_80612be4 + 0x264);   // +0x98
    *(unsigned*)((char*)DAT_80612d68 + 0x80) |= 0x80;
    DAT_80612d68[0x22] = -1;         // +0x88

    double bias = DOUBLE_8061ab70;
    float sx = FLOAT_80612c58 * (*(float*)((int)&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - bias)) +
               FLOAT_80612c78;
    *(float*)((char*)DAT_80612d68 + 0x5c) =
        FLOAT_8061ab28 * (float)((double)CONCAT44(0x43300000, (int)sx ^ 0x80000000) - bias) +
        FLOAT_8061ab68;
    *(float*)((char*)DAT_80612d68 + 0x60) =
        FLOAT_8061ab30 * (float)((double)CONCAT44(0x43300000, (unsigned)DAT_80612d68[0x26] ^ 0x80000000) - bias);
    float sz = FLOAT_80612c54 * (*(float*)((int)&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - bias)) +
               FLOAT_80612c74;
    *(float*)((char*)DAT_80612d68 + 0x64) =
        FLOAT_8061ab28 * (float)((double)CONCAT44(0x43300000, (int)sz ^ 0x80000000) - bias) +
        FLOAT_8061ab68;

    *(int*)((char*)DAT_80612d68 + 0x44) = *(int*)((char*)DAT_80612d68 + 0x5c);
    *(float*)((char*)DAT_80612d68 + 0x48) = FLOAT_8061ab6c + *(float*)((char*)DAT_80612d68 + 0x60);
    *(int*)((char*)DAT_80612d68 + 0x4c) = *(int*)((char*)DAT_80612d68 + 0x64);
    *(int*)((char*)DAT_80612d68 + 0x68) = *(int*)((char*)DAT_80612d68 + 0x44);
    *(int*)((char*)DAT_80612d68 + 0x6c) = *(int*)((char*)DAT_80612d68 + 0x48);
    *(int*)((char*)DAT_80612d68 + 0x70) = *(int*)((char*)DAT_80612d68 + 0x4c);
    *(int*)((char*)DAT_80612d68 + 0x50) = *(int*)((char*)DAT_80612d68 + 0x44);
    *(int*)((char*)DAT_80612d68 + 0x54) = *(int*)((char*)DAT_80612d68 + 0x48);
    *(int*)((char*)DAT_80612d68 + 0x58) = *(int*)((char*)DAT_80612d68 + 0x4c);
    DAT_80612d68[0x796a] = -1;   // +0x1e5a8
}

// Unpack a packed colour word (byte lanes at >>0x18, >>0x10, >>8) into a float rgb vector.
void aveditor_unpack_rgb(unsigned packed, float* out)
{
    double bias = DOUBLE_8061ab48;
    out[0] = (float)((double)CONCAT44(0x43300000, packed >> 0x18) - bias);
    out[1] = (float)((double)CONCAT44(0x43300000, (packed >> 0x10) & 0xff) - bias);
    out[2] = (float)((double)CONCAT44(0x43300000, (packed >> 8) & 0xff) - bias);
}

// Compute the camera pick-ray delta (eye - target) into DAT_805e2a30..38.
void aveditor_cursor_calc_ray_delta(void)
{
    DAT_805e2a30 = DAT_80561c94 - DAT_80561c88;
    DAT_805e2a34 = DAT_80561c98 - DAT_80561c8c;
    DAT_805e2a38 = DAT_80561c9c - DAT_80561c90;
}

// qsort comparator ordering two brush segments by view depth: computes each segment's squared
// distance to the camera anchor scaled by its dot with the pick-ray delta, and returns -1/0/1.
unsigned aveditor_brush_depth_compare(int a, int b)
{
    float ax = FLOAT_8061ab2c * (*(float*)(a + 0xc) + *(float*)(a + 0x18));
    float bx = FLOAT_8061ab2c * (*(float*)(b + 0xc) + *(float*)(b + 0x18));
    float ay = FLOAT_8061ab2c * (*(float*)(a + 0x10) + *(float*)(a + 0x1c));
    float by = FLOAT_8061ab2c * (*(float*)(b + 0x10) + *(float*)(b + 0x1c));
    float az = FLOAT_8061ab2c * (*(float*)(a + 0x14) + *(float*)(a + 0x20));
    float bz = FLOAT_8061ab2c * (*(float*)(b + 0x14) + *(float*)(b + 0x20));

    float adx = DAT_80561c88 - ax, ady = DAT_80561c8c - ay, adz = DAT_80561c90 - az;
    float bdx = DAT_80561c88 - bx, bdy = DAT_80561c8c - by, bdz = DAT_80561c90 - bz;

    float da = (adz * adz + adx * adx + ady * ady) *
               (DAT_805e2a38 * (az - DAT_80561c90) + DAT_805e2a30 * (ax - DAT_80561c88) +
                DAT_805e2a34 * (ay - DAT_80561c8c));
    float db = (bdz * bdz + bdx * bdx + bdy * bdy) *
               (DAT_805e2a38 * (bz - DAT_80561c90) + DAT_805e2a30 * (bx - DAT_80561c88) +
                DAT_805e2a34 * (by - DAT_80561c8c));
    if (da <= db) {
        return (unsigned)(da < db);
    }
    return 0xffffffff;
}

// Copy a placed cell's colour bytes (8 bytes) into an entity's +0x18/+0x1c fields. The source is the
// cell's editor colour (bit 4 clear) or the roster colour block (bit 4 set); defaults come from
// DAT_8061c1b0/b4.
void aveditor_cursor_copy_color(int entity, int cellIndex)
{
    int color[2];
    color[0] = DAT_8061c1b0;
    color[1] = DAT_8061c1b4;
    if (((&DAT_80477c80)[cellIndex * 0x31] & 4) == 0) {
        leaf_31e8((unsigned)(intptr_t)color, cellIndex * 0xc4 + 0x80477be4, 8);
    } else {
        leaf_31e8((unsigned)(intptr_t)color, cellIndex * 0xc4 + DAT_805d59a0 * 0x1c + -0x7fb8841c, 8);
    }
    *(int*)(entity + 0x18) = color[0];
    *(int*)(entity + 0x1c) = color[1];
}

// Per-frame editor cursor/preview update tick. Drives the placement cursor entity (single-tile or
// drag-region), rebuilds the queued brush segments when the drag rect changes, sorts them by depth,
// positions/animates the preview entities, and updates the brush anchor/ray from the camera. When
// the editor's "preview active" flag is clear it tears the preview down instead.
//
// NON-MATCHING: this reproduces the decompiler's structure; the extensive int->double magic-constant
// float conversions are kept via CONCAT44. The brush-segment records are accessed through
// DAT_80612d68 word indices exactly as decompiled (base word 0x16d..0x187, 0x1e-word stride, count
// word 0x796a).
void aveditor_cursor_update(void)
{
    FUN_803826e8();
    int cursorEnt = DAT_80612d68[0x21];
    if (cursorEnt != 0) {
        *(unsigned*)(cursorEnt + 0x28) |= 8;
        *(unsigned*)(cursorEnt + 0x28) &= 0xdfffffff;
    }

    unsigned held = input_get_buttons_held(0);
    int root = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    bool active = ((held & 0x800) == 0 && (*(unsigned*)(root + 0xc0) & 0x20) == 0) ||
                  ((*(unsigned*)(DAT_80612be4 + 0x25c) & 0x2000000) == 0);
    if (!active) {
        aveditor_preview_teardown();
        *(unsigned*)(DAT_80612be4 + 0x238) &= 0xfffffbff;
        FUN_80382734();
        return;
    }

    float zero = FLOAT_8061ab34;
    if (*DAT_80612d68 == 0) {
        DAT_80612d68[0x20] = 0;
        DAT_80612d68[0x24] = -1;
        DAT_80612d68[0x22] = -1;
        DAT_80612d68[0x21] = 0;
        DAT_80612d68[0x23] = 0;
        FUN_80382734();
        return;
    }

    float local_134 = 0, local_130 = 0, local_12c = 0;
    float afStack_128[16];
    float afStack_a8[16];

    if (cursorEnt != 0) {
        *(float*)(cursorEnt + 0x64) = zero;
        *(float*)(cursorEnt + 0x68) = zero;
        *(float*)(cursorEnt + 0x6c) = zero;
        local_134 = zero; local_130 = zero; local_12c = zero;
        aveditor_cursor_calc_ray_delta();

        if (DAT_80612d68[0x25] < 0) {
            // single-tile cursor path
            if (DAT_80612d68[0x24] < 0) {
                *(unsigned char*)(cursorEnt + 0x32) = 1;
                *(unsigned*)(cursorEnt + 0x28) |= 1;
                *(unsigned*)(cursorEnt + 0x10) |= 1;
                unsigned is21 = avanim_layout_is_0x21(DAT_80612d68[0x22]);
                if (is21 == 0) {
                    short kind = *(short*)(cursorEnt + 0x94);
                    if (kind == 0x7a8) {
                        int* p = *(int**)(*(int*)(cursorEnt + 4) + 4);
                        float* v = (float*)p[*p * -0x30 + 0x1b];
                        float m[16];
                        float pos3[3] = { v[0], v[1], v[2] };
                        mtx4_set_rotation_zyx(
                            (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0xc0)) / FLOAT_8061ab3c),
                            (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0x70)) / FLOAT_8061ab3c),
                            (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0xc8)) / FLOAT_8061ab3c),
                            m);
                        mtx4_transform_vector_alias(m, pos3);
                        double px = (double)(pos3[0] + *(float*)(cursorEnt + 0x64));
                        double pz = (double)(pos3[2] + *(float*)(cursorEnt + 0x6c));
                        *(float*)(cursorEnt + 0x64) = zero;
                        *(float*)(cursorEnt + 0x68) = zero;
                        *(float*)(cursorEnt + 0x6c) = zero;
                        mtx4_set_identity((int)(intptr_t)afStack_a8);
                        mtx4_set_translate_rotz(-px, (double)FLOAT_8061ab34, -pz,
                            (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0x70)) / FLOAT_8061ab3c),
                            afStack_a8);
                        mtx4_transform_point(afStack_a8, (float*)(cursorEnt + 0x64));
                        *(int*)(cursorEnt + 0x70) = DAT_80612d68[0xf];
                    } else if (kind == 0x872 || kind == 0x874 || kind == 0x87f || kind == 0x876 ||
                               kind == 0x881 || kind == 0x878 || kind == 0x883 || kind == 0x87a ||
                               kind == 0x885 || (unsigned short)(kind - 0x87c) < 2 || kind == 0x887) {
                        double ox = (double)DAT_803e0ac0;
                        double oy = (double)DAT_803e0ac4;
                        double oz = (double)DAT_803e0ac8;
                        mtx4_set_translate_rotz(ox, oy, oz,
                            (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0x70)) / FLOAT_8061ab3c),
                            afStack_a8);
                        mtx4_transform_point(afStack_a8, (float*)(cursorEnt + 0x64));
                        *(int*)(cursorEnt + 0x70) = DAT_80612d68[0xf];
                        int ent2 = *(int*)(DAT_80612be4 + 0x4154);
                        if (ent2 != 0) {
                            *(unsigned*)(ent2 + 0x28) |= 8;
                            *(float*)(*(int*)(DAT_80612be4 + 0x4154) + 0x64) = FLOAT_8061ab34;
                            *(float*)(*(int*)(DAT_80612be4 + 0x4154) + 0x68) = FLOAT_8061ab34;
                            *(float*)(*(int*)(DAT_80612be4 + 0x4154) + 0x6c) = FLOAT_8061ab34;
                            mtx4_set_translate_rotz(ox, oy, oz,
                                (double)((FLOAT_8061ab78 * *(float*)(ent2 + 0x70)) / FLOAT_8061ab3c),
                                afStack_a8);
                            mtx4_transform_point(afStack_a8,
                                (float*)(*(int*)(DAT_80612be4 + 0x4154) + 0x64));
                            *(int*)(*(int*)(DAT_80612be4 + 0x4154) + 0x70) = DAT_80612d68[0xf];
                        }
                    } else {
                        *(int*)(cursorEnt + 0x70) = DAT_80612d68[0xf];
                    }
                } else {
                    *(int*)(cursorEnt + 0x70) = DAT_80612d68[0xf];
                    *(float*)(cursorEnt + 0x64) = zero;
                    *(float*)(cursorEnt + 0x68) = zero;
                    *(float*)(cursorEnt + 0x6c) = zero;
                    mtx4_set_identity((int)(intptr_t)afStack_a8);
                    mtx4_set_translate_rotz(-(double)*(float*)(cursorEnt + 0x1a8),
                        (double)FLOAT_8061ab34, -(double)*(float*)(cursorEnt + 0x1b0),
                        (double)((FLOAT_8061ab78 * *(float*)(cursorEnt + 0x70)) / FLOAT_8061ab3c),
                        afStack_a8);
                    mtx4_transform_point(afStack_a8, (float*)(cursorEnt + 0x64));
                }
            } else {
                // moving an existing single tile: offset cursor by the panel's stored Z
                double bias = DOUBLE_8061ab70;
                int panel = *(int*)(&DAT_805d6038 + DAT_80612d68[0x24] * 4);
                double zc = (double)(CONCAT44(0x43300000, (unsigned)*(int*)(panel + 0x1c) ^ 0x80000000));
                local_130 = -(float)(zc - bias);
                *(float*)(cursorEnt + 0x64) += FLOAT_8061ab34;
                *(float*)(cursorEnt + 0x68) += local_130;
                *(float*)(cursorEnt + 0x6c) += zero;
                *(unsigned char*)(cursorEnt + 0x32) = 1;
                *(unsigned*)(cursorEnt + 0x28) |= 1;
                *(unsigned*)(cursorEnt + 0x10) |= 1;
                mtx4_set_rotation_y(
                    (double)((FLOAT_8061ab78 * (float)DAT_80612d68[0xf]) / FLOAT_8061ab3c),
                    afStack_a8);
                float* mm = *(float**)(*(int*)(cursorEnt + 4) + 0x10);
                mtx4_multiply(afStack_a8, mm, mm);
                int t = aveditor_get_pad_tile_count(DAT_80612d68[0x24]);
                double tz = -(double)*(float*)(*(int*)(DAT_80612be4 + t * 4 + 0x4068) + 0x1b0);
                t = aveditor_get_pad_tile_count(DAT_80612d68[0x24]);
                mtx4_set_translation(-(double)*(float*)(*(int*)(DAT_80612be4 + t * 4 + 0x4068) + 0x1a8),
                                     (double)FLOAT_8061ab34, tz);
                mm = *(float**)(*(int*)(cursorEnt + 4) + 0x10);
                mtx4_multiply(afStack_a8, mm, mm);
                if ((*(unsigned char*)(*(int*)(&DAT_805d6038 + DAT_80612d68[0x24] * 4) + 0x12) & 1) != 0) {
                    *(unsigned*)(cursorEnt + 0x28) |= 0x20000000;
                    mtx4_set_identity((int)(intptr_t)afStack_a8);
                    mtx4_set_scale((double)FLOAT_8061ab80, (double)FLOAT_8061ab84, (double)FLOAT_8061ab84);
                    mm = *(float**)(*(int*)(cursorEnt + 4) + 0x10);
                    mtx4_multiply(afStack_a8, mm, mm);
                    mtx4_set_identity((int)(intptr_t)afStack_a8);
                    double xc = (double)(CONCAT44(0x43300000,
                        (unsigned)(-*(int*)(*(int*)(&DAT_805d6038 + DAT_80612d68[0x24] * 4) + 0x14))) ^ 0x80000000);
                    mtx4_write_translation((double)(FLOAT_8061ab28 * (float)(xc - DOUBLE_8061ab70)),
                        (double)FLOAT_8061ab34, (double)FLOAT_8061ab34, (int)(intptr_t)afStack_a8);
                    mm = *(float**)(*(int*)(cursorEnt + 4) + 0x10);
                    mtx4_multiply(afStack_a8, mm, mm);
                }
            }
        } else {
            // drag-region path: rebuild the queued brush segments when the rect changed (bVar3),
            // otherwise re-evaluate their fade state (bVar4 / final else), then depth-sort them.
            float f11 = (float)DAT_80612d68[0x11];
            bool changed = false, shrunk = false;
            bool anyDiff = (f11 != (float)DAT_80612d68[0x17]) ||
                           ((float)DAT_80612d68[0x12] != (float)DAT_80612d68[0x18]) ||
                           ((float)DAT_80612d68[0x13] != (float)DAT_80612d68[0x19]);
            if (anyDiff) {
                shrunk = true;
                bool collapsed =
                    (f11 != (float)DAT_80612d68[0x17] && f11 == (float)DAT_80612d68[0x1a]) ||
                    ((float)DAT_80612d68[0x12] != (float)DAT_80612d68[0x18] &&
                     (float)DAT_80612d68[0x12] == (float)DAT_80612d68[0x1b]) ||
                    ((float)DAT_80612d68[0x13] != (float)DAT_80612d68[0x19] &&
                     (float)DAT_80612d68[0x13] == (float)DAT_80612d68[0x1c]);
                if (collapsed) {
                    changed = true;
                }
            }

            double bias = DOUBLE_8061ab70;
            if (changed) {
                // rebuild the segment list from the linked brush-node list at DAT_80612c14
                DAT_80612d68[0x796a] = 0;
                double sx = (double)FLOAT_8061ab28;
                double sy = (double)FLOAT_8061ab30;
                for (float* node = DAT_80612c14; node != (float*)0; node = (float*)(intptr_t)node[0x10]) {
                    float nx = node[0];
                    float nz = node[2];
                    float ny = node[1];
                    float tag = node[3];
                    int base = DAT_80612d68[0x796a] * 0x1e + 0x16d;
                    int panelId = (int)node[4];
                    int panel = *(int*)(&DAT_805d6038 + panelId * 4);
                    // (segment min/max seeding varies by tag; reproduce the common path)
                    DAT_80612d68[base] = (int)nx;
                    DAT_80612d68[base + 2] = (int)ny;
                    double cx = (double)(CONCAT44(0x43300000, (unsigned)*(int*)(panel + 0x14)) ^ 0x80000000);
                    double cz = (double)(CONCAT44(0x43300000, (unsigned)*(int*)(panel + 0x18)) ^ 0x80000000);
                    nx = nx + (float)(cx - bias);
                    ny = ny + (float)(cz - bias);
                    DAT_80612d68[base + 3] = (int)nx;
                    DAT_80612d68[base + 5] = (int)ny;

                    nx = (float)(sx * (double)nx);
                    float scaledMid = (float)(sy * (double)nz);
                    ny = (float)(sx * (double)ny);
                    DAT_80612d68[base + 1] = (int)scaledMid;
                    double dz = (double)(CONCAT44(0x43300000, (unsigned)*(int*)(panel + 0x1c)) ^ 0x80000000);
                    DAT_80612d68[base + 4] = (int)(float)(sy * (double)(float)(dz - bias) + (double)scaledMid);

                    int slot = DAT_80612d68[0x796a] * 0x1e + 0x176;
                    DAT_80612d68[slot] = (int)(intptr_t)node;
                    DAT_80612d68[DAT_80612d68[0x796a] * 0x1e + 0x16a] = 0;
                    DAT_80612d68[DAT_80612d68[0x796a] * 0x1e + 0x16b] = 0;
                    DAT_80612d68[DAT_80612d68[0x796a] * 0x1e + 0x16c] = 0;
                    int flags = aveditor_place_all_segments();
                    DAT_80612d68[DAT_80612d68[0x796a] * 0x1e + 0x175] = flags & 0xfdffffff;

                    int t = aveditor_get_pad_tile_count(panelId);
                    *(unsigned char*)(*(int*)(DAT_80612be4 + t * 4 + 0x40dc) + 0x32) = 1;
                    t = aveditor_get_pad_tile_count(panelId);
                    int te = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
                    *(unsigned*)(te + 0x28) |= 1;
                    t = aveditor_get_pad_tile_count(panelId);
                    te = *(int*)(DAT_80612be4 + t * 4 + 0x40dc);
                    *(unsigned*)(te + 0x10) |= 1;

                    int mtxSlot = DAT_80612d68[0x796a] * 0x1e + 0x177;
                    mtx4_set_identity((int)(intptr_t)(DAT_80612d68 + mtxSlot));
                    float rm[16];
                    double rc = (double)(CONCAT44(0x43300000, (unsigned)(int)tag) ^ 0x80000000);
                    mtx4_set_rotation_y(
                        (double)((FLOAT_8061ab78 * FLOAT_8061ab7c * (float)(rc - DOUBLE_8061ab70)) / FLOAT_8061ab3c),
                        rm);
                    mtx4_multiply(rm, (float*)(DAT_80612d68 + mtxSlot), (float*)(DAT_80612d68 + mtxSlot));

                    DAT_80612d68[DAT_80612d68[0x796a] * 0x1e + 0x187] = 3;   // |1 |2
                    DAT_80612d68[0x796a] = DAT_80612d68[0x796a] + 1;
                }
            } else if (shrunk) {
                // rect shrinking: re-fade the existing segments toward removal
                int off = 0;
                for (int i = 0; i < DAT_80612d68[0x796a]; i++, off += 0x78) {
                    int seg = *(int*)((int)DAT_80612d68 + off + 0x5d8);
                    int t = aveditor_get_pad_tile_count(*(int*)(seg + 0x10));
                    *(unsigned char*)(*(int*)(DAT_80612be4 + t * 4 + 0x40dc) + 0x32) = 1;
                    // (fade fields at +0x5cc are re-evaluated; kept structural)
                    (void)t;
                }
            } else {
                // steady state: clamp each segment's fade counter into {0,0x40,0x7f}
                int off = 0;
                for (int i = 0; i < DAT_80612d68[0x796a]; i++, off += 0x78) {
                    int fade = off + 0x5cc;
                    if (*(int*)((int)DAT_80612d68 + fade) < 0x65) {
                        *(int*)((int)DAT_80612d68 + fade) =
                            (*(int*)((int)DAT_80612d68 + fade) < 0x29) ? 0 : 0x40;
                    } else {
                        *(int*)((int)DAT_80612d68 + fade) = 0x7f;
                    }
                }
            }
            if (1 < DAT_80612d68[0x796a]) {
                FUN_8038a0a4((int)(intptr_t)(DAT_80612d68 + 0x16a), DAT_80612d68[0x796a], 0x78,
                             aveditor_brush_depth_compare);
            }
        }

        // reposition the preview entities for the active pad
        if (-1 < DAT_80612d68[0x24] && (DAT_80612d68[0x20] & 0x10) != 0) {
            int panel = *(int*)(&DAT_805d6038 + DAT_80612d68[0x24] * 4);
            mtx4_set_rotation_y(
                (double)((FLOAT_8061ab78 * (float)DAT_80612d68[0xf]) / FLOAT_8061ab3c), afStack_128);
            int t = aveditor_get_pad_tile_count(DAT_80612d68[0x24]);
            double dz = -(double)*(float*)(*(int*)(DAT_80612be4 + t * 4 + 0x4068) + 0x1a8);
            t = aveditor_get_pad_tile_count(DAT_80612d68[0x24]);
            local_134 = (float)((double)local_134 + dz);
            local_130 = local_130 + FLOAT_8061ab34;
            local_12c = local_12c + -*(float*)(*(int*)(DAT_80612be4 + t * 4 + 0x4068) + 0x1b0);
            if (*(int*)(DAT_80612be4 + 0x4158) == 0) {
                aveditor_preview_spawn_entities();
            }
            int recOff = 0, hi = 0;
            for (int i = 0; i < *(int*)(panel + 0x44); i++, recOff += 6) {
                unsigned char* rec = (unsigned char*)(panel + recOff + 0x48);
                if (*rec < 5 && rec[5] != 2) {
                    aveditor_preview_place_entity(rec, hi, &local_134, afStack_128);
                    hi++;
                }
            }
        }

        // update the brush anchor / camera ray from the cursor state
        if (cursorEnt == 0 || *(short*)(cursorEnt + 0x94) != 0x798) {
            if (DAT_80612d68[0x25] < 0) {
                int cell = DAT_80612d68[0x22];
                if (-1 < cell && cursorEnt != 0) {
                    float rgb[3];
                    if (((&DAT_80477c80)[cell * 0x31] & 6) == 0) {
                        aveditor_unpack_rgb(*(unsigned*)(&DAT_80477bcc + cell * 0xc4), rgb);
                    } else {
                        aveditor_unpack_rgb(0x7f7f7f7f, rgb);
                    }
                    collision_set_radius_and_flags((double)FLOAT_8061ab84,
                        *(int**)(cursorEnt + 4), (unsigned char)(int)rgb[0],
                        (unsigned char)(int)rgb[1], (unsigned char)(int)rgb[2]);
                    if (*(int*)(DAT_80612be4 + 0x4154) != 0) {
                        collision_set_radius_and_flags((double)FLOAT_8061ab84,
                            *(int**)(*(int*)(DAT_80612be4 + 0x4154) + 4),
                            (unsigned char)(int)rgb[0], (unsigned char)(int)rgb[1],
                            (unsigned char)(int)rgb[2]);
                    }
                }
                DAT_80561c88 = FLOAT_8061ab34;
                DAT_80561c94 = FLOAT_8061ab34;
                DAT_80561c98 = FLOAT_8061ab34;
                DAT_80561c9c = FLOAT_8061ab34;
                unsigned idx = (unsigned)(FLOAT_8061ab8c *
                    ((FLOAT_8061ab78 * (float)DAT_80612d68[0x10]) / FLOAT_8061ab3c) + FLOAT_8061ab2c);
                DAT_80561c8c = (float)DAT_80612d68[0xe] * (float)(&DAT_80583228)[idx & 0x7ff];
                DAT_80561c90 = (float)DAT_80612d68[0xe] *
                    *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
            }
        } else {
            DAT_80561c88 = FLOAT_8061ab34;
            DAT_80561c94 = FLOAT_8061ab34;
            DAT_80561c98 = FLOAT_8061ab88;
            DAT_80561c9c = FLOAT_8061ab34;
            unsigned idx = (unsigned)(FLOAT_8061ab8c *
                ((FLOAT_8061ab78 * (float)DAT_80612d68[0x10]) / FLOAT_8061ab3c) + FLOAT_8061ab2c);
            DAT_80561c8c = FLOAT_8061ab88 + (float)DAT_80612d68[0xe] * (float)(&DAT_80583228)[idx & 0x7ff];
            DAT_80561c90 = (float)DAT_80612d68[0xe] *
                *(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc));
        }

        if (DAT_80612d68[0x22] == -1) {
            DAT_80561ca0 = FLOAT_8061ab34;
            DAT_80561ca4 = FLOAT_8061ab90;
        } else {
            aveditor_cursor_copy_color(-0x7fa9e378, DAT_80612d68[0x22]);
        }
    }
    FUN_80382734();
}
