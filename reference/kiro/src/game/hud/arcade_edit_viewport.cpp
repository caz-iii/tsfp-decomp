// Batch: batch_890 | Module: arcade map-editor grid viewport / camera transform
// Address range: 0x80314c24 - 0x803151e4
// Date: 2026-09-12
//
// The 2D viewport/camera layer for the arcade map editor's top-down grid view. It converts editor
// world coordinates to grid pixel coordinates (scale FLOAT_80612c60 x / FLOAT_80612c5c y about the
// world origin FLOAT_80612c78 / FLOAT_80612c74, offset by the magic-double-converted pixel origins
// DAT_80612c88 / DAT_80612c84), draws primitives in that space, computes the placed-map bounding
// box, fits the view to it, and supports scale save/restore and panning. The block of grid-view
// state lives contiguously at 0x80612c34..0x80612c88.
//
//   aveditor_draw_grid_line     0x80314c24  draw a line in editor world coords (transformed).
//   aveditor_draw_grid_quad     0x80314ccc  draw a 4-vertex quad in editor world coords.
//   aveditor_compute_bounds     0x80314e94  compute the bounding box over all placed members.
//   aveditor_recompute_scale    0x80314f94  recompute the world->pixel scale + derived extents.
//   aveditor_fit_view_to_bounds 0x80315044  fit the view to the placed-map bounds.
//   aveditor_view_from_camera   0x80315130  seed the view from the camera record DAT_80611748.
//   aveditor_save_view_scale    0x80315174  save the current scale into DAT_805d60f0.
//   aveditor_restore_view_scale 0x8031519c  restore the saved scale from DAT_805d60f0.
//   aveditor_set_view_scale     0x803151c4  set the world->pixel scale directly.
//   aveditor_pan_view           0x803151e4  translate the world origin (pan the view).
//
// NON-MATCHING notes:
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double conversion idiom is written with the
//    shared CONCAT44 / tsfp_concat44 intrinsic from common.h, matching the disassembly.
//  * aveditor_draw_grid_line calls gx_draw_line_f(x0, y0, x1, y1, color) - a clean 5-arg match.
//  * aveditor_draw_grid_quad calls gx_draw_quad_f4, whose project-canonical signature is
//    (unsigned long long frameArgs, double y0, double x1, double y1, double x2, double y2, double x3,
//    double y3) - the leading x0 coordinate occupies the frameArgs slot (the decompiler treats it as
//    a register-passed value, same pattern as gx_draw_fill_rect_f in batch_889). This batch passes
//    the transformed x0 cast into that leading slot followed by the 7 remaining transformed coords.
//  * The member "next" link is word index 0x10 (+0x40) of the member record; the member's geometry
//    dims record is at member[member[3]+0xb] with pixel-space extents at +0x14 / +0x18.
//
// Raw FLOAT_/DOUBLE_/DAT_ symbols get correctly-typed per-file externs below. Named helpers
// (gx_draw_line_f, gx_draw_quad_f4) and this batch's own functions resolve through the shared
// headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- grid-view transform state (contiguous block 0x80612c34..0x80612c88) --
extern float FLOAT_80612c34;  // view extent y (span)
extern float FLOAT_80612c38;  // view extent x (span)
extern float FLOAT_80612c3c;  // view pad y
extern float FLOAT_80612c40;  // view pad x
extern float FLOAT_80612c44;  // bounds max y
extern float FLOAT_80612c48;  // bounds max x
extern float FLOAT_80612c4c;  // bounds min y
extern float FLOAT_80612c50;  // bounds min x
extern float FLOAT_80612c54;  // inverse y scale (1px in world y)
extern float FLOAT_80612c58;  // inverse x scale (1px in world x)
extern float FLOAT_80612c5c;  // world->pixel y scale
extern float FLOAT_80612c60;  // world->pixel x scale
extern float FLOAT_80612c64;  // viewport height (world units)
extern float FLOAT_80612c68;  // viewport width (world units)
extern float FLOAT_80612c6c;  // view far y (origin + height)
extern float FLOAT_80612c70;  // view far x (origin + width)
extern float FLOAT_80612c74;  // world origin y
extern float FLOAT_80612c78;  // world origin x
extern int   DAT_80612c7c;    // viewport pixel height (int, magic-double converted)
extern int   DAT_80612c80;    // viewport pixel width  (int, magic-double converted)
extern int   DAT_80612c84;    // pixel origin y (int, magic-double converted)
extern int   DAT_80612c88;    // pixel origin x (int, magic-double converted)

// -- active member list + camera record --
extern float *DAT_80612c14;   // active member list head (next link at word 0x10 / +0x40)
extern int   *DAT_80611748;   // camera record (pixel origin x/y at [0]/[5], size w/h at [3]/[8])

// -- saved scale slot --
extern float DAT_805d60f0;    // saved world->pixel x scale
extern float DAT_805d60f4;    // saved world->pixel y scale
extern float DAT_805d60f8;    // saved inverse x scale
extern float DAT_805d60fc;    // saved inverse y scale

// -- conversion + layout constants --
extern double DOUBLE_8061a6f0; // int->float conversion bias (0x43300000 magic)
extern float  FLOAT_8061a6e8;  // 1.0 (unit for inverse-scale)
extern float  FLOAT_8061a6f8;  // large sentinel for bounds min
extern float  FLOAT_8061a6fc;  // small sentinel (-large) for bounds max
extern float  FLOAT_8061a700;  // view pad constant
extern float  FLOAT_8061a704;  // fit viewport width constant
extern float  FLOAT_8061a708;  // fit centering default
extern float  FLOAT_8061a70c;  // fit centering half factor (0.5)
extern float  FLOAT_8061a710;  // fit centering sentinel
extern float  FLOAT_8061a714;  // fit x margin

} // extern "C"

// Map a world x coordinate into grid pixel space.
static inline double aveditor_world_to_px_x(double wx)
{
    return (double)(FLOAT_80612c60 * (float)(wx - (double)FLOAT_80612c78) +
           (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
}

// Map a world y coordinate into grid pixel space.
static inline double aveditor_world_to_px_y(double wy)
{
    return (double)(FLOAT_80612c5c * (float)(wy - (double)FLOAT_80612c74) +
           (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
}

// Draw a line between two world-space endpoints (transformed into grid pixel space).
void aveditor_draw_grid_line(double x0, double y0, double x1, double y1, unsigned int color)
{
    gx_draw_line_f(aveditor_world_to_px_x(x0), aveditor_world_to_px_y(y0),
                   aveditor_world_to_px_x(x1), aveditor_world_to_px_y(y1), color);
    return;
}

// Draw a 4-vertex quad given by four world-space (x,y) pairs (transformed into grid pixel space).
// (NON-MATCHING: gx_draw_quad_f4's leading coord occupies the frameArgs slot - see header note.)
void aveditor_draw_grid_quad(double x0, double y0, double x1, double y1,
                             double x2, double y2, double x3, double y3)
{
    gx_draw_quad_f4((unsigned long long)aveditor_world_to_px_x(x0),
                    aveditor_world_to_px_y(y0),
                    aveditor_world_to_px_x(x1), aveditor_world_to_px_y(y1),
                    aveditor_world_to_px_x(x2), aveditor_world_to_px_y(y2),
                    aveditor_world_to_px_x(x3), aveditor_world_to_px_y(y3));
    return;
}

// Compute the bounding box over every placed member: min/max of each member's world (x,y) plus its
// pixel-space extents (dims record at member[member[3]+0xb], extents at +0x14 / +0x18). Records the
// box in FLOAT_80612c44..c50 and the resulting span in FLOAT_80612c34 / c38.
void aveditor_compute_bounds(void)
{
    float x;
    float y;
    float xMax;
    float yMax;
    int *member;   // member record: word 0/1 are float x/y, word 3 dims index, word 0x10 next link

    FLOAT_80612c4c = FLOAT_8061a6f8;
    FLOAT_80612c50 = FLOAT_8061a6f8;
    FLOAT_80612c44 = FLOAT_8061a6fc;
    FLOAT_80612c48 = FLOAT_8061a6fc;
    for (member = (int *)DAT_80612c14; member != (int *)0x0; member = *(int **)(member + 0x10)) {
        int dims = member[member[3] + 0xb];  // dims record pointer (raw slot value)
        x = *(float *)&member[0];
        y = *(float *)&member[1];
        xMax = x + (float)((double)CONCAT44(0x43300000,
               *(u32 *)(dims + 0x14) ^ 0x80000000) - DOUBLE_8061a6f0);
        yMax = y + (float)((double)CONCAT44(0x43300000,
               *(u32 *)(dims + 0x18) ^ 0x80000000) - DOUBLE_8061a6f0);
        if (x < FLOAT_80612c50) {
            FLOAT_80612c50 = x;
        }
        if (FLOAT_80612c48 < xMax) {
            FLOAT_80612c48 = xMax;
        }
        if (y < FLOAT_80612c4c) {
            FLOAT_80612c4c = y;
        }
        if (FLOAT_80612c44 < yMax) {
            FLOAT_80612c44 = yMax;
        }
    }
    FLOAT_80612c40 = FLOAT_8061a700;
    FLOAT_80612c38 = FLOAT_80612c48 - FLOAT_80612c50;
    FLOAT_80612c34 = FLOAT_80612c44 - FLOAT_80612c4c;
    FLOAT_80612c3c = FLOAT_8061a700;
    return;
}

// Recompute the world->pixel scale factors and their inverses from the current viewport size
// (pixel dims DAT_80612c80 x / DAT_80612c7c y over viewport width FLOAT_80612c68).
void aveditor_recompute_scale(void)
{
    FLOAT_80612c70 = FLOAT_80612c78 + FLOAT_80612c68;
    FLOAT_80612c64 = FLOAT_80612c68 *
        ((float)((double)CONCAT44(0x43300000, (u32)DAT_80612c7c ^ 0x80000000) - DOUBLE_8061a6f0) /
         (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c80 ^ 0x80000000) - DOUBLE_8061a6f0));
    FLOAT_80612c60 =
        (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c80 ^ 0x80000000) - DOUBLE_8061a6f0) /
        FLOAT_80612c68;
    FLOAT_80612c6c = FLOAT_80612c74 + FLOAT_80612c64;
    FLOAT_80612c5c =
        (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c7c ^ 0x80000000) - DOUBLE_8061a6f0) /
        FLOAT_80612c64;
    FLOAT_80612c58 = FLOAT_8061a6e8 / FLOAT_80612c60;
    FLOAT_80612c54 = FLOAT_8061a6e8 / FLOAT_80612c5c;
    return;
}

// Fit the view to the placed-map bounds: recompute bounds, size the viewport, center the map, then
// recompute the scale factors.
void aveditor_fit_view_to_bounds(void)
{
    float cx;
    float cy;

    aveditor_compute_bounds();
    FLOAT_80612c68 = FLOAT_8061a704;
    FLOAT_80612c64 = FLOAT_8061a704 *
        ((float)((double)CONCAT44(0x43300000, (u32)DAT_80612c7c ^ 0x80000000) - DOUBLE_8061a6f0) /
         (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c80 ^ 0x80000000) - DOUBLE_8061a6f0));
    cx = FLOAT_8061a708;
    if (FLOAT_8061a710 != FLOAT_80612c50 + FLOAT_80612c48) {
        cx = (FLOAT_80612c50 + FLOAT_80612c48) * FLOAT_8061a70c;
    }
    cy = FLOAT_8061a708;
    if (FLOAT_8061a710 != FLOAT_80612c4c + FLOAT_80612c44) {
        cy = (FLOAT_80612c4c + FLOAT_80612c44) * FLOAT_8061a70c;
    }
    FLOAT_80612c78 = cx - FLOAT_8061a714;
    FLOAT_80612c74 = -(FLOAT_80612c64 * FLOAT_8061a70c - cy);
    aveditor_recompute_scale();
    return;
}

// Seed the grid view from the camera record DAT_80611748 (pixel origin x/y at [0]/[5], viewport
// size w/h at [3]/[8]), then fit the view to the bounds.
void aveditor_view_from_camera(void)
{
    DAT_80612c88 = *DAT_80611748;
    DAT_80612c84 = DAT_80611748[5];
    DAT_80612c80 = DAT_80611748[3];
    DAT_80612c7c = DAT_80611748[8];
    aveditor_fit_view_to_bounds();
    return;
}

// Save the current world->pixel scale + inverse-scale into the scratch slot DAT_805d60f0.
void aveditor_save_view_scale(void)
{
    DAT_805d60f0 = FLOAT_80612c60;
    DAT_805d60f4 = FLOAT_80612c5c;
    DAT_805d60f8 = FLOAT_80612c58;
    DAT_805d60fc = FLOAT_80612c54;
    return;
}

// Restore the world->pixel scale + inverse-scale from the scratch slot DAT_805d60f0.
void aveditor_restore_view_scale(void)
{
    FLOAT_80612c60 = DAT_805d60f0;
    FLOAT_80612c5c = DAT_805d60f4;
    FLOAT_80612c58 = DAT_805d60f8;
    FLOAT_80612c54 = DAT_805d60fc;
    return;
}

// Set the world->pixel scale directly (x = param_1, y = param_2), deriving the inverse scales.
void aveditor_set_view_scale(double scaleX, double scaleY)
{
    FLOAT_80612c60 = (float)scaleX;
    FLOAT_80612c5c = (float)scaleY;
    FLOAT_80612c58 = (float)((double)FLOAT_8061a6e8 / scaleX);
    FLOAT_80612c54 = (float)((double)FLOAT_8061a6e8 / scaleY);
    return;
}

// Pan the view by translating the world origin by (dx, dy), keeping the far corner consistent.
void aveditor_pan_view(double dx, double dy)
{
    double originX;
    double originY;

    originX = (double)FLOAT_80612c78;
    originY = (double)FLOAT_80612c74;
    FLOAT_80612c78 = (float)(originX + dx);
    FLOAT_80612c74 = (float)(originY + dy);
    FLOAT_80612c70 = (float)(originX + dx) + FLOAT_80612c68;
    FLOAT_80612c6c = (float)(originY + dy) + FLOAT_80612c64;
    return;
}
