// Batch: batch_891 | Module: arcade map-editor cell/sprite render layer
// Address range: 0x80315218 - 0x80316d18
// Date: 2026-09-12
//
// The render layer for the arcade map editor's top-down grid view. It draws the placed member
// cells as sprite quads, overlays selection/cursor markers and count labels, renders the direction
// arrows for spawn/objective pieces, and animates the "active piece" pulse. It sits directly on top
// of the viewport transform (arcade_edit_viewport.cpp) and the GX sprite/text helpers. Editor state
// lives in the context block at DAT_80612be4; per-cell geometry keys into the static tables
// &DAT_80477bca (category, stride 0xc4) and &DAT_8051a834 (grid layout, stride 0x1e0).
//
//   aveditor_draw_cell_count_label 0x80315218  draw the count digit label over a cell (if font fits).
//   aveditor_draw_cell_marker      0x8031536c  draw a cursor/selection marker sprite over a cell.
//   aveditor_draw_cell_sprite      0x80315870  draw a member cell as a sprite quad (+ optional label).
//   aveditor_render_all_members    0x80315d8c  iterate every slot/cell and render its sprite + label.
//   aveditor_draw_paste_outline    0x8031607c  draw the paste-preview outline rectangles.
//   aveditor_draw_selection_frame  0x80316248  draw the selection frame (paste mode or per-region).
//   aveditor_draw_spawn_arc        0x803162f0  draw the spawn/objective facing arc + endpoints.
//   aveditor_draw_cell_icon        0x803165c4  draw a cell's type icon quad (+ facing arc + label).
//   aveditor_render_member_cells   0x80316b70  render every cell of one member then its label pass.
//   aveditor_tick_pulse            0x80316d18  advance the active-piece highlight pulse animation.
//
// NON-MATCHING notes:
//  * Several functions are emitted with the compiler _savegpr_/_restgpr_ runtime helpers (rendered
//    as FUN_803826dc/FUN_803826e8/FUN_803826f0/__save_gpr_25 etc.) that spill callee-saved GPRs and
//    fold the incoming register pair into an `undefined8` "return" which the decompiler then splits
//    via `>>0x20`. The real ABI (a short* cell-coord pointer in r3 and a float*/int* member record
//    in r4, recovered from the call sites) is expressed here as explicit parameters (Compile-Clean
//    rules 1/3). Their prototypes are set on the IDB via set_function_prototype.
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double conversion idiom is written with the
//    shared CONCAT44 / tsfp_concat44 intrinsic from common.h.
//  * Member records mix float coords (words 0/1) with raw pointer/index slots (word 3 dims index,
//    word 0xf cell list, word 0x10 next); they are walked through an int*/short* view with the float
//    fields read by reinterpretation, avoiding the decompiler's illegal float-array pointer casts.
//  * gx_draw_sprite_quad takes 8 args (4 double coords + 4 uint u/v); calls match that signature.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below. Named helpers
// (text_set_font_table, text_draw_char_shadowed, gx_draw_sprite_quad, gx_set_active_material,
// cursor_get_texture_id, sprite_get_texture_id, debug_log_formatted, aveditor_get_slot_texset,
// aveditor_draw_grid_rect, mtx4_*, math_poly_trig, rng_counter_tick) and this batch's own functions
// resolve through the shared headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- editor context + viewport transform state --
extern int   DAT_80612be4;    // editor context base
extern float FLOAT_80612c54;  // inverse y scale
extern float FLOAT_80612c58;  // inverse x scale
extern float FLOAT_80612c5c;  // world->pixel y scale
extern float FLOAT_80612c60;  // world->pixel x scale
extern float FLOAT_80612c74;  // world origin y
extern float FLOAT_80612c78;  // world origin x
extern int   DAT_80612c84;    // pixel origin y (magic-double)
extern int   DAT_80612c88;    // pixel origin x (magic-double)
extern float FLOAT_80611674;  // text baseline adjust
extern int   DAT_806109c4;    // current zoom / scale numerator (magic-double)
extern int   DAT_806109c0;    // alt scale numerator (magic-double)
extern int   DAT_806108e4;    // pulse phase base
extern int   DAT_80612028;    // global frame/time counter
extern unsigned char DAT_80611a51; // selection highlight alpha byte

// -- static tables --
extern short DAT_80477bca;    // category table (short at [type*0x62]) - selection-shape flag
extern int   DAT_80477bcc;    // category table (word at +2 of same 0xc4-stride record) - color base
extern int   DAT_8051a834;    // grid layout table X (stride 0x1e0)
extern int   DAT_8051a838;    // grid layout table Y (stride 0x1e0)
extern int   DAT_806119ec;    // active grid layout index
extern int   DAT_8047fb68;    // texture-set handle table (0x32 entries)
extern int  *DAT_80610a20;    // sprite material record table base (0x58 stride)
extern int   DAT_80611190;    // printf-style format string for count labels

// -- conversion + layout constants --
extern double DOUBLE_8061a6f0; // int->float conversion bias (0x43300000 magic)
extern float  FLOAT_8061a6e8;  // 1.0
extern float  FLOAT_8061a700;  // text x pad
extern float  FLOAT_8061a70c;  // 0.5 (half)
extern float  FLOAT_8061a710;  // 0.0 sentinel
extern float  FLOAT_8061a718;  // label zoom threshold
extern float  FLOAT_8061a71c;  // count label x offset
extern float  FLOAT_8061a720;  // count label y offset
extern float  FLOAT_8061a724;  // marker centering offset
extern float  FLOAT_8061a728;  // char x pad
extern float  FLOAT_8061a72c;  // sprite centering offset
extern float  FLOAT_8061a730;  // sprite half-size
extern float  FLOAT_8061a734;  // outline inset (thin)
extern float  FLOAT_8061a738;  // outline size (full)
extern float  FLOAT_8061a73c;  // arc radius base
extern float  FLOAT_8061a740;  // arc angle scale
extern float  FLOAT_8061a744;  // arc chord scale
extern float  FLOAT_8061a748;  // arc step
extern float  FLOAT_8061a74c;  // arc z base
extern float  FLOAT_8061a750;  // arc end pad
extern float  FLOAT_8061a754;  // arc endpoint x
extern float  FLOAT_8061a758;  // arc endpoint z
extern float  FLOAT_8061a75c;  // icon zoom threshold A
extern float  FLOAT_8061a760;  // fade alpha scale
extern float  FLOAT_8061a764;  // fade range scale
extern float  FLOAT_8061a768;  // icon zoom threshold B
extern float  FLOAT_8061a76c;  // pulse default extent
extern float  FLOAT_8061a770;  // pulse phase scale
extern float  FLOAT_8061a774;  // pulse sine freq
extern float  FLOAT_8061a778;  // pulse sine phase

// -- raw helpers not yet named --
extern void FUN_8032da30(double x, double y, double scale);                 // draw wildcard marker
extern int  FUN_80387fb8(double a, double b, double c, double d, double e,  // format count into buf
                         double f, unsigned long long g, unsigned long long h,
                         int buf, int fmt, int value, int p6, int p7, int p8, int p9, int p10);
extern int  FUN_80342530(short *coord);                                     // classify cell coord
extern int  FUN_80329068(int slot);                                         // resolve slot record
extern int  FUN_80321a64(int rec);                                          // resolve material id
extern int  FUN_80336690(int member, int index);                           // get cell record by idx
extern void FUN_8032568c(float *lo, float *hi);                            // clamp arc angle range
extern void FUN_8031a578(double cx, double cy, double x0, double y0, double x1, double y1); // arc seg
extern void FUN_8031a38c(double cx, double cy, double a, double b, double c, double d, double e); // arc cap
extern int  FUN_803826f0(void);   // compiler _savegpr_ helper; returns the active piece record ptr

} // extern "C"

// Draw the numeric count label centered over a cell (only when the current zoom shows text). A
// value of -1 draws the "wildcard" marker instead of a digit.
void aveditor_draw_cell_count_label(double cellX, double cellY, int member,
                                    unsigned int p4, unsigned int p5, unsigned int p6,
                                    unsigned int p7, unsigned int p8, unsigned int p9,
                                    unsigned int p10)
{
    int prevFont;
    int value;
    char buf[128];
    double px;
    double py;
    double sx;

    prevFont = text_set_font_table(0x12);
    value = *(int *)(member + 0x18);
    if (value != 0) {
        sx = (double)FLOAT_80612c60;
        if ((double)((float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) -
                     DOUBLE_8061a6f0) / FLOAT_8061a718) < sx) {
            double dy = (double)((float)(cellY - (double)FLOAT_8061a720) - FLOAT_80612c74);
            double dx = (double)(float)(sx * (double)((float)((double)FLOAT_8061a71c + cellX) -
                        FLOAT_80612c78) +
                        (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                        DOUBLE_8061a6f0));
            double tx = (double)(float)(dx - (double)FLOAT_8061a700);
            double ty = (double)(float)((double)FLOAT_80612c5c * dy +
                        (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                        DOUBLE_8061a6f0));
            if (value == -1) {
                FUN_8032da30(tx, ty, (double)FLOAT_8061a6e8);
            }
            else {
                FUN_80387fb8(dy, (double)FLOAT_80612c5c, (double)FLOAT_8061a700, dx,
                             DOUBLE_8061a6f0, sx, 0, 0,
                             (int)buf, (int)&DAT_80611190, value, p6, p7, p8, p9, p10);
                text_draw_char_shadowed(tx, ty, (int)buf, 0x7f7f7f80, 0xf0f0f40);
            }
        }
        text_set_font_table(prevFont);
    }
    return;
}

// Draw a cursor/selection marker over a cell. coord is the cell's (x,y,index) short triple; member
// is the member record (float coords at words 0/1, dims index at word 3). When param4 is nonzero it
// also draws a count label under the marker.
// (NON-MATCHING: coord/member arrive as the r3/r4 pair folded by the _savegpr_ spill.)
void aveditor_draw_cell_marker(short *coord, int *member, int param3, int param4)
{
    int cellIndex;
    int classified;
    bool valid;
    int prevFont;
    double markX;
    double markY;
    float zoom;

    valid = false;
    cellIndex = (int)((float)((double)(CONCAT44(0x43300000,
                 *(u32 *)(DAT_80612be4 + 0x264)) ^ 0x80000000) - DOUBLE_8061a6f0) -
                 *(float *)&member[2]);
    if ((unsigned int)cellIndex < *(u32 *)(member[member[3] + 0xb] + 0x1c)) {
        short key[3];
        key[0] = coord[0];
        key[1] = coord[1];
        key[2] = coord[2];
        classified = FUN_80342530(key);
        if (classified == 0) {
            int gx = (int)(FLOAT_80612c58 *
                     (*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                     DOUBLE_8061a6f0)) + FLOAT_80612c78);
            int gy = (int)(FLOAT_80612c54 *
                     (*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                     DOUBLE_8061a6f0)) + FLOAT_80612c74);
            valid = true;
            markX = (double)((float)((double)(CONCAT44(0x43300000, gx) ^ 0x80000000) -
                    DOUBLE_8061a6f0) + FLOAT_8061a70c);
            markY = (double)((float)((double)CONCAT44(0x43300000, (u32)gy ^ 0x80000000) -
                    DOUBLE_8061a6f0) + FLOAT_8061a70c);
        }
        else {
            markX = (double)((float)((double)CONCAT44(0x43300000, (u32)(int)coord[0] ^ 0x80000000) -
                    DOUBLE_8061a6f0) + *(float *)&member[0]);
            markY = (double)((float)((double)CONCAT44(0x43300000, (u32)(int)coord[1] ^ 0x80000000) -
                    DOUBLE_8061a6f0) + *(float *)&member[1]);
            if ((int)coord[2] == cellIndex) {
                valid = true;
            }
        }
        if (valid) {
            prevFont = text_set_font_table(0x12);
            double sx = (double)FLOAT_80612c60;
            double sy = (double)FLOAT_80612c5c;
            unsigned int qx = (unsigned int)(sx * (double)(((float)((double)FLOAT_8061a70c + markX) -
                              FLOAT_8061a724) - FLOAT_80612c78) +
                              (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                              DOUBLE_8061a6f0));
            unsigned int qy = (unsigned int)(sy * (double)(((float)((double)FLOAT_8061a70c + markY) -
                              FLOAT_8061a724) - FLOAT_80612c74) +
                              (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                              DOUBLE_8061a6f0));
            int mat = cursor_get_texture_id(0x31);
            gx_set_active_material(mat);
            int ex = (int)((double)FLOAT_8061a70c * sx);
            int ey = (int)((double)FLOAT_8061a70c * sy);
            gx_draw_sprite_quad(
                (double)(float)((double)CONCAT44(0x43300000, qx ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, qy ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, (qx + ex) ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, (qy + ey) ^ 0x80000000) - DOUBLE_8061a6f0),
                0, 0, 0x100, 0x100);
            zoom = (float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) -
                   DOUBLE_8061a6f0) / FLOAT_8061a718;
            if ((FLOAT_80612c60 <= zoom) || (param4 == 0)) {
                if ((param3 == 0x1020407f) && (zoom < FLOAT_80612c60)) {
                    int spr = sprite_get_texture_id(3);
                    double half = (double)FLOAT_8061a70c;
                    double size = (double)(float)((double)(float)(half * (double)FLOAT_80612c60) * half);
                    double dx = (double)(float)((double)FLOAT_80612c60 *
                                (double)((float)(markX - half) - FLOAT_80612c78) +
                                (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                                DOUBLE_8061a6f0));
                    double dx2 = (double)(float)(dx + size);
                    double dy = (double)(FLOAT_80612c5c * ((float)(markY - half) - FLOAT_80612c74) +
                                (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                                DOUBLE_8061a6f0));
                    gx_set_active_material(spr);
                    int rec = *DAT_80610a20 + spr * 0x58;
                    gx_draw_sprite_quad(dx, dy, dx2, (double)(float)(dy + size), 0, 0,
                        (unsigned int)*(unsigned short *)(rec + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(rec + 0x52) << 4);
                }
            }
            else {
                text_draw_char_shadowed(
                    (double)(FLOAT_8061a700 + FLOAT_80612c60 *
                     ((float)(markX - (double)FLOAT_8061a728) - FLOAT_80612c78) +
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0)),
                    (double)((FLOAT_80612c5c * ((float)((double)FLOAT_8061a6e8 + markY) - FLOAT_80612c74) +
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0)) -
                     FLOAT_80611674),
                    param4, 0x7f7f7f80, 0xf0f0f40);
            }
            text_set_font_table(prevFont);
        }
    }
    return;
}

// Draw a member cell as a sprite quad using the given material, optionally overlaying a count label
// (param4) or the "off-grid" preview when param3 marks a highlighted cell.
// (NON-MATCHING: coord/member arrive as the r3/r4 pair folded by the _savegpr_ spill.)
void aveditor_draw_cell_sprite(short *coord, int *member, int param3, int param4, int material)
{
    int cellIndex;
    int classified;
    bool valid;
    int prevFont;
    double markX;
    double markY;
    float zoom;

    valid = false;
    cellIndex = (int)((float)((double)(CONCAT44(0x43300000,
                 *(u32 *)(DAT_80612be4 + 0x264)) ^ 0x80000000) - DOUBLE_8061a6f0) -
                 *(float *)&member[2]);
    if ((unsigned int)cellIndex < *(u32 *)(member[member[3] + 0xb] + 0x1c)) {
        short key[3];
        key[0] = coord[0];
        key[1] = coord[1];
        key[2] = coord[2];
        classified = FUN_80342530(key);
        if (classified == 0) {
            int gx = (int)(FLOAT_80612c58 *
                     (*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                     DOUBLE_8061a6f0)) + FLOAT_80612c78);
            int gy = (int)(FLOAT_80612c54 *
                     (*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                     DOUBLE_8061a6f0)) + FLOAT_80612c74);
            valid = true;
            markX = (double)((float)((double)(CONCAT44(0x43300000, gx) ^ 0x80000000) -
                    DOUBLE_8061a6f0) + FLOAT_8061a70c);
            markY = (double)((float)((double)CONCAT44(0x43300000, (u32)gy ^ 0x80000000) -
                    DOUBLE_8061a6f0) + FLOAT_8061a70c);
        }
        else {
            markX = (double)((float)((double)CONCAT44(0x43300000, (u32)(int)coord[0] ^ 0x80000000) -
                    DOUBLE_8061a6f0) + *(float *)&member[0]);
            markY = (double)((float)((double)CONCAT44(0x43300000, (u32)(int)coord[1] ^ 0x80000000) -
                    DOUBLE_8061a6f0) + *(float *)&member[1]);
            if ((int)coord[2] == cellIndex) {
                valid = true;
            }
        }
        if (valid) {
            prevFont = text_set_font_table(0x12);
            double sx = (double)FLOAT_80612c60;
            double sy = (double)FLOAT_80612c5c;
            unsigned int qx = (unsigned int)(sx * (double)(((float)((double)FLOAT_8061a70c + markX) -
                              FLOAT_8061a72c) - FLOAT_80612c78) +
                              (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                              DOUBLE_8061a6f0));
            unsigned int qy = (unsigned int)(sy * (double)(((float)((double)FLOAT_8061a70c + markY) -
                              FLOAT_8061a72c) - FLOAT_80612c74) +
                              (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                              DOUBLE_8061a6f0));
            gx_set_active_material(material);
            int ex = (int)((double)FLOAT_8061a730 * sx);
            int ey = (int)((double)FLOAT_8061a730 * sy);
            int rec = *DAT_80610a20 + material * 0x58;
            gx_draw_sprite_quad(
                (double)(float)((double)CONCAT44(0x43300000, qx ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, qy ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, (qx + ex) ^ 0x80000000) - DOUBLE_8061a6f0),
                (double)(float)((double)CONCAT44(0x43300000, (qy + ey) ^ 0x80000000) - DOUBLE_8061a6f0),
                0, 0,
                (unsigned int)*(unsigned short *)(rec + 0x4e) << 4,
                (unsigned int)*(unsigned short *)(rec + 0x52) << 4);
            zoom = (float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) -
                   DOUBLE_8061a6f0) / FLOAT_8061a718;
            if ((FLOAT_80612c60 <= zoom) || (param4 == 0)) {
                if ((param3 == 0x1020407f) && (zoom < FLOAT_80612c60)) {
                    int spr = sprite_get_texture_id(3);
                    double half = (double)FLOAT_8061a70c;
                    double size = (double)(float)((double)(float)(half * (double)FLOAT_80612c60) * half);
                    double dx = (double)(float)((double)FLOAT_80612c60 *
                                (double)((float)(markX - half) - FLOAT_80612c78) +
                                (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) -
                                DOUBLE_8061a6f0));
                    double dx2 = (double)(float)(dx + size);
                    double dy = (double)(FLOAT_80612c5c * ((float)(markY - half) - FLOAT_80612c74) +
                                (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) -
                                DOUBLE_8061a6f0));
                    gx_set_active_material(spr);
                    int rec2 = *DAT_80610a20 + spr * 0x58;
                    gx_draw_sprite_quad(dx, dy, dx2, (double)(float)(dy + size), 0, 0,
                        (unsigned int)*(unsigned short *)(rec2 + 0x4e) << 4,
                        (unsigned int)*(unsigned short *)(rec2 + 0x52) << 4);
                }
            }
            else {
                text_draw_char_shadowed(
                    (double)(FLOAT_8061a700 + FLOAT_80612c60 *
                     ((float)(markX - (double)FLOAT_8061a728) - FLOAT_80612c78) +
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0)),
                    (double)((FLOAT_80612c5c * ((float)((double)FLOAT_8061a6e8 + markY) - FLOAT_80612c74) +
                     (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0)) -
                     FLOAT_80611674),
                    param4, 0x7f7f7f80, 0xf0f0f40);
            }
            text_set_font_table(prevFont);
        }
    }
    return;
}

// Iterate every editor slot and each of its cells, drawing the cell sprite + optional count label,
// plus the active-selection cell overlay. (NON-MATCHING: the many undefined8 accumulator temps are
// the format-helper return threading; expressed as plain calls with the format buffer.)
void aveditor_render_all_members(void)
{
    int slotOff;
    int slot;
    int material;
    int slotRec;
    short activeSlot;
    int color;

    text_set_font_table(0x12);
    slot = 0;
    slotOff = 0;
    while (slot < *(int *)(DAT_80612be4 + 0x280)) {
        slotRec = DAT_80612be4 + slotOff + 0x288;
        material = FUN_80329068(slot);
        material = FUN_80321a64(material);
        int cellOff = 0;
        for (int i = 0; i < *(int *)(slotRec + 0x58); i = i + 1) {
            int *cell = (int *)(slotRec + cellOff + 100);
            color = aveditor_get_slot_texset(slot);
            int marker = 0;
            activeSlot = *(short *)(DAT_80612be4 + 0x2f10);
            int active = (int)activeSlot;
            if ((activeSlot < 0) || (slot == active)) {
                if (*(int *)(DAT_80612be4 + 0x254) != 3) {
                    color = (color & 0xffffff00) | 0x28;
                }
            }
            else {
                color = (color & 0xffffff00) | 0x3f;
            }
            if ((activeSlot < 0) || (active == slot)) {
                bool extra = false;
                if ((slot == active) && (*(short *)(DAT_80612be4 + 0x2f12) <= i)) {
                    extra = true;
                }
                marker = i + 1;
                if (extra) {
                    marker = i + 2;
                }
            }
            if (marker == 0) {
                short coord[3];
                coord[0] = (short)cell[1];
                coord[1] = (short)(cell[1] >> 16);
                coord[2] = *(short *)(cell + 2);
                if (i == 0) {
                    aveditor_draw_cell_sprite((short *)&cell[1], (int *)cell[0] ? (int *)&cell[0] : (int *)&cell[0], color, 0, material);
                }
                else {
                    aveditor_draw_cell_marker((short *)&cell[1], (int *)&cell[0], color, 0);
                }
            }
            else if (i == 0) {
                if (*(int *)(slotRec + 0x58) < 2) {
                    aveditor_draw_cell_sprite((short *)&cell[1], (int *)&cell[0], color, 0, material);
                }
                else {
                    debug_log_formatted((char *)&DAT_80611190, 0, 0, 0, 0, 0, 0, 0,
                                        marker, active, DAT_80612be4, 0, 0, 0, 0, 0);
                    aveditor_draw_cell_sprite((short *)&cell[1], (int *)&cell[0], color, 0, material);
                }
            }
            else {
                debug_log_formatted((char *)&DAT_80611190, 0, 0, 0, 0, 0, 0, 0,
                                    marker, active, DAT_80612be4, 0, 0, 0, 0, 0);
                aveditor_draw_cell_marker((short *)&cell[1], (int *)&cell[0], color, 0);
            }
            cellOff = cellOff + 0x10;
        }
        color = aveditor_get_slot_texset(slot);
        if (((slot == *(short *)(DAT_80612be4 + 0x2f10)) && (*(int *)(DAT_80612be4 + 0x2f18) != 0)) &&
            (*(int *)(DAT_80612be4 + 0x254) != 4)) {
            unsigned int hi = (aveditor_get_slot_texset(slot) & 0xffffff00) | (unsigned int)DAT_80611a51;
            if (*(short *)(DAT_80612be4 + 0x2f12) == 0) {
                aveditor_draw_cell_sprite((short *)(DAT_80612be4 + 0x2f28),
                                          *(int **)(DAT_80612be4 + 0x2f18), hi, 0, material);
            }
            else {
                debug_log_formatted((char *)&DAT_80611190, 0, 0, 0, 0, 0, 0, 0,
                                    *(short *)(DAT_80612be4 + 0x2f12) + 1, color, 0, 0,
                                    DAT_80612be4, 0, 0, 0);
                aveditor_draw_cell_marker((short *)(DAT_80612be4 + 0x2f28),
                                          *(int **)(DAT_80612be4 + 0x2f18), hi, 0);
            }
        }
        slot = slot + 1;
        slotOff = slotOff + 0xe4;
    }
    text_set_font_table(0x12);
    return;
}

// Draw the paste-preview outline: for each cell of the current paste piece whose z matches the view
// layer, draw a 5-rectangle box outline in grid space.
void aveditor_draw_paste_outline(int piece)
{
    short *coord;
    float *pcell;
    unsigned int color;
    int count;
    int off;
    double x0;
    double y0;
    double d;

    count = *(int *)(piece + 0x20);
    off = 0;
    color = *(u32 *)(&DAT_8047fb68 + ((unsigned int)piece % 0x32) * 4);
    for (int i = 0; i < count; i = i + 1) {
        coord = (short *)FUN_80336690(piece, i);
        pcell = *(float **)(piece + off + 0x24);
        if (*(int *)(DAT_80612be4 + 0x264) == (int)coord[2] + (int)*(float *)&pcell[2]) {
            x0 = (double)((float)((double)(CONCAT44(0x43300000, (int)coord[0]) ^ 0x80000000) -
                 DOUBLE_8061a6f0) + pcell[0] + FLOAT_8061a734);
            y0 = (double)((float)((double)CONCAT44(0x43300000, (u32)(int)coord[1] ^ 0x80000000) -
                 DOUBLE_8061a6f0) + pcell[1] + FLOAT_8061a734);
            aveditor_draw_grid_rect((color & 0xffffff00) | 0xf, x0, y0,
                (double)(float)(x0 + (double)FLOAT_8061a738),
                (double)(float)(y0 + (double)FLOAT_8061a738));
            color = (color & 0xffffff00) | 0x2d;
            aveditor_draw_grid_rect(color, x0, y0,
                (double)(float)(x0 + (double)FLOAT_8061a738),
                (double)(float)(y0 + (double)FLOAT_8061a734));
            d = (double)(float)(x0 + (double)FLOAT_8061a738);
            aveditor_draw_grid_rect(color, (double)(float)(d - (double)FLOAT_8061a734), y0, d,
                (double)(float)(y0 + (double)FLOAT_8061a738));
            d = (double)(float)(y0 + (double)FLOAT_8061a738);
            aveditor_draw_grid_rect(color, x0, (double)(float)(d - (double)FLOAT_8061a734),
                (double)(float)(x0 + (double)FLOAT_8061a738), d);
            aveditor_draw_grid_rect(color, x0, y0,
                (double)(float)(x0 + (double)FLOAT_8061a734),
                (double)(float)(y0 + (double)FLOAT_8061a738));
        }
        off = off + 8;
    }
    return;
}

// Draw the selection frame: in paste mode (state 7) draw the paste outline once; otherwise draw an
// outline for every region record whose state == 3.
void aveditor_draw_selection_frame(void)
{
    int off;

    if (*(int *)(DAT_80612be4 + 0x1185c) == 7) {
        aveditor_draw_paste_outline(0);
    }
    else {
        off = 0;
        for (int i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
            if (*(int *)(DAT_80612be4 + off + 0xdd94) == 3) {
                aveditor_draw_paste_outline(0);
            }
            off = off + 0x8c;
        }
    }
    return;
}

// Draw the spawn/objective facing arc: sweep a wedge of segment quads across the piece's facing
// angle range, then cap both endpoints. (NON-MATCHING: piece record arrives folded via the
// _savegpr_ spill; the many in_fNN temps are callee-saved FP registers with no semantic effect.)
void aveditor_draw_spawn_arc(double centerX, double centerY, int *piece)
{
    float fLo;
    float fHi;
    bool more;
    int type;
    float aLo;
    float aHi;
    double angle;
    double next;
    double step;
    double zBase;
    double xBase;

    type = *piece;
    double base = -(double)(FLOAT_8061a73c *
                  (float)((double)CONCAT44(0x43300000, (u32)piece[2] ^ 0x80000000) - DOUBLE_8061a6f0));
    if ((type == 0x35) || (type == 0x36) || (type == 0x37)) {
        aHi = FLOAT_8061a740 * -(float)piece[7];
        aLo = FLOAT_8061a740 * (float)piece[7];
        FUN_8032568c(&aHi, &aLo);
        fLo = (FLOAT_8061a744 * (float)(base + (double)aHi)) / FLOAT_8061a740;
        fHi = (FLOAT_8061a744 * (float)(base + (double)aLo)) / FLOAT_8061a740;
    }
    else {
        fLo = (FLOAT_8061a744 * (float)(base - (double)(FLOAT_8061a740 * (float)piece[6]))) /
              FLOAT_8061a740;
        fHi = (FLOAT_8061a744 * (float)(base + (double)(FLOAT_8061a740 * (float)piece[6]))) /
              FLOAT_8061a740;
    }
    double a0 = (double)fLo;
    double a1 = (double)fHi;
    step = (double)FLOAT_8061a748;
    zBase = (double)FLOAT_8061a710;
    xBase = (double)FLOAT_8061a74c;
    angle = a0;
    more = true;
    while (more) {
        float v[16];
        next = (double)(float)(step + angle);
        if (a1 < (double)(float)(step + angle)) {
            more = false;
            next = a1;
        }
        v[0] = (float)zBase;   // local_f4
        v[3] = (float)zBase;   // local_f0 slot pattern (kept as in decomp)
        v[2] = (float)xBase;   // local_ec
        mtx4_set_identity((int)v);
        mtx4_set_rotation_y(angle, v);
        mtx4_transform_point(v, &v[0]);
        float w[16];
        w[0] = (float)zBase;
        w[2] = (float)xBase;
        mtx4_set_identity((int)w);
        mtx4_set_rotation_y(next, w);
        mtx4_transform_point(w, &w[0]);
        double p0x = (double)v[0] + centerX;
        double p0y = (double)v[2] + centerY;
        double p1x = (double)w[0] + centerX;
        double p1y = (double)w[2] + centerY;
        FUN_8031a578(centerX, centerY, p0x, p0y, p1x, p1y);
        angle = next;
    }
    FUN_8031a38c(centerX, centerY, (double)FLOAT_8061a710, (double)FLOAT_8061a754,
                 (double)FLOAT_8061a74c, (double)FLOAT_8061a758,
                 (double)(float)(a0 + (double)FLOAT_8061a750));
    FUN_8031a38c(centerX, centerY, (double)FLOAT_8061a710, (double)FLOAT_8061a754,
                 (double)FLOAT_8061a74c, (double)FLOAT_8061a758,
                 (double)(float)(a1 + (double)FLOAT_8061a750));
    return;
}

// Draw a cell's type icon quad at the given grid position, fading with zoom, optionally overlaying
// the spawn arc (for spawn/objective piece types) and a count label. (NON-MATCHING: piece record
// arrives folded via the _savegpr_ spill; extraout_f1 is the incoming FP arg = icon x.)
void aveditor_draw_cell_icon(double iconX, double iconY, int *piece, unsigned int material,
                             unsigned int param5, unsigned int param6, unsigned int param7,
                             unsigned int param8, unsigned int param9, unsigned int param10)
{
    float zoom;
    int type;
    unsigned int alpha;
    int spr;
    int rec;
    double cx;
    double cy;

    type = *piece;
    cx = (double)(float)((double)FLOAT_8061a70c + iconX);
    cy = (double)(float)((double)FLOAT_8061a70c + iconY);
    unsigned int selWild = 0;
    if ((*(int *)(DAT_80612be4 + 0x254) == 4) && (*(int *)(DAT_80612be4 + 0x2f34) == -1)) {
        selWild = 1;
    }
    unsigned int notSel = countLeadingZeros(selWild);
    if (FLOAT_80612c60 <
        (float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a6f0) /
        FLOAT_8061a75c) {
        int spr0 = sprite_get_texture_id(3);
        float thresh = (float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) -
                       DOUBLE_8061a6f0) / FLOAT_8061a768;
        double ex = (double)(FLOAT_8061a730 * FLOAT_80612c60);
        double ey = (double)(FLOAT_8061a730 * FLOAT_80612c5c);
        double dx = (double)(FLOAT_80612c60 * ((float)(cx - (double)FLOAT_8061a72c) - FLOAT_80612c78) +
                    (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
        double dy = (double)(FLOAT_80612c5c * ((float)(cy - (double)FLOAT_8061a72c) - FLOAT_80612c74) +
                    (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
        alpha = material;
        if (thresh <= FLOAT_80612c60) {
            alpha = ((0x80U - (int)(FLOAT_8061a760 *
                    (((FLOAT_80612c60 - thresh) * FLOAT_8061a764) /
                    (((float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) - DOUBLE_8061a6f0) /
                    FLOAT_8061a75c - thresh) * FLOAT_8061a764)))) & 0xff) | (material & 0xffffff00);
        }
        gx_set_active_material(spr0);
        rec = *DAT_80610a20 + spr0 * 0x58;
        gx_draw_sprite_quad(dx, dy, (double)(float)(dx + ex), (double)(float)(dy + ey), 0, 0,
            (unsigned int)*(unsigned short *)(rec + 0x4e) << 4,
            (unsigned int)*(unsigned short *)(rec + 0x52) << 4);
        material = alpha;
    }
    if ((float)((double)(CONCAT44(0x43300000, (u32)DAT_806109c4) ^ 0x80000000) - DOUBLE_8061a6f0) /
        FLOAT_8061a768 <= FLOAT_80612c60) {
        double ex = (double)(FLOAT_8061a730 * FLOAT_80612c60);
        double ey = (double)(FLOAT_8061a730 * FLOAT_80612c5c);
        double dx = (double)(FLOAT_80612c60 * ((float)(cx - (double)FLOAT_8061a72c) - FLOAT_80612c78) +
                    (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
        double dy = (double)(FLOAT_80612c5c * ((float)(cy - (double)FLOAT_8061a72c) - FLOAT_80612c74) +
                    (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
        int mat = FUN_80321a64(type);
        double sx = (double)FLOAT_80612c60;
        if (sx < (double)((float)((double)CONCAT44(0x43300000, (u32)DAT_806109c4 ^ 0x80000000) -
            DOUBLE_8061a6f0) / FLOAT_8061a75c)) {
            material = (material & 0xffffff00) | (0x80 - (material & 0xff));
        }
        if (type == 0x45) {
            double sy = (double)FLOAT_80612c5c;
            double ix = (double)(float)(sx * (double)((float)(cx - (double)FLOAT_8061a724) - FLOAT_80612c78) +
                        (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
            double iy = (double)(float)(sy * (double)((float)(cy - (double)FLOAT_8061a724) - FLOAT_80612c74) +
                        (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
            int cmat = cursor_get_texture_id(0x31);
            gx_set_active_material(cmat);
            gx_draw_sprite_quad(ix, iy,
                (double)(float)(ix + (double)(float)((double)FLOAT_8061a70c * sx)),
                (double)(float)(iy + (double)(float)((double)FLOAT_8061a70c * sy)), 0, 0, 0x100, 0x100);
        }
        else {
            gx_set_active_material(mat);
            unsigned int uv = (unsigned int)*(unsigned short *)(*DAT_80610a20 + mat * 0x58 + 0x4e) << 4;
            gx_draw_sprite_quad(dx, dy, (double)(float)(dx + ex), (double)(float)(dy + ey), 0, 0, uv, uv);
            if ((((unsigned int)(type - 0x32) < 5) || (type == 0x37)) &&
                (*(int **)(DAT_80612be4 + 0x2f1c) == piece) && ((notSel >> 5) != 0)) {
                aveditor_draw_spawn_arc(cx, cy, piece);
            }
        }
        if (param5 != 0) {
            int prevFont = text_set_font_table(0x12);
            if ((float)((double)(CONCAT44(0x43300000, (u32)DAT_806109c4) ^ 0x80000000) - DOUBLE_8061a6f0) /
                FLOAT_8061a718 < FLOAT_80612c60) {
                int spr2 = sprite_get_texture_id(3);
                double half = (double)FLOAT_8061a70c;
                double size = (double)(float)((double)(float)(half * (double)FLOAT_80612c60) * half);
                double lx = (double)(float)((double)FLOAT_80612c60 *
                            (double)((float)(cx - half) - FLOAT_80612c78) +
                            (double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0));
                double lx2 = (double)(float)(lx + size);
                double ly = (double)(FLOAT_80612c5c * ((float)(cy - half) - FLOAT_80612c74) +
                            (float)((double)CONCAT44(0x43300000, (u32)DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0));
                gx_set_active_material(spr2);
                rec = *DAT_80610a20 + spr2 * 0x58;
                gx_draw_sprite_quad(lx, ly, lx2, (double)(float)(ly + size), 0, 0,
                    (unsigned int)*(unsigned short *)(rec + 0x4e) << 4,
                    (unsigned int)*(unsigned short *)(rec + 0x52) << 4);
                material = param5;
            }
            text_set_font_table(prevFont);
        }
        if ((&DAT_80477bca)[type * 0x62] == 1) {
            aveditor_draw_cell_count_label(cx, cy, (int)piece, param6, param7, param8, material,
                                           param8, param9, param10);
        }
    }
    return;
}

// Render every cell of one member (member record at param9) drawing each cell icon, then a final
// label pass. (NON-MATCHING: extraout register threading between icon calls is dropped.)
void aveditor_render_member_cells(int *member, unsigned int param11, int param12, int param13,
                                  unsigned int param14, unsigned int param15, int param16)
{
    int viewLayer;
    int *cell;
    double dvA;
    double dvB;

    viewLayer = *(int *)(DAT_80612be4 + 0x264);
    dvB = (double)(float)((double)(CONCAT44(0x43300000, viewLayer) ^ 0x80000000) - DOUBLE_8061a6f0);
    if ((unsigned int)(int)(dvB - (double)*(float *)&member[2]) <
        *(u32 *)(member[member[3] + 0xb] + 0x1c)) {
        dvA = 0.0;
        dvB = 0.0;
        for (cell = *(int **)&member[0xf]; cell != (int *)0x0; cell = (int *)cell[5]) {
            int localX = (int)*(short *)(cell + 3);
            int localY = (int)*(short *)((int)cell + 0xe);
            param12 = localX;
            param11 = (unsigned int)(localX + (int)*(float *)&member[0]);
            param14 = (unsigned int)(short)param11;
            param15 = (unsigned int)(short)(localY + (short)(int)*(float *)&member[1]);
            if ((int)(short)(*(short *)(cell + 4) + (short)(int)*(float *)&member[2]) ==
                *(int *)(DAT_80612be4 + 0x264)) {
                param13 = *cell * 0xc4;
                param16 = *(int *)(DAT_80612be4 + 0x254);
                unsigned int neg = 0xffffffff - (cell[1] >> 0x1f);
                unsigned int color = *(u32 *)(&DAT_80477bcc + param13);
                param11 = neg & 0x5064787f;
                if ((((param16 == 4) && (*(int *)(DAT_80612be4 + 0x1185c) != 2)) || (param16 == 3)) &&
                    ((color = (color & 0xffffff00) | 0x28), param11 != 0)) {
                    param11 = (neg & 0x50647800) | 0x28;
                }
                double px = (double)(float)((double)CONCAT44(0x43300000, param14 ^ 0x80000000) - DOUBLE_8061a6f0);
                double py = (double)(float)((double)CONCAT44(0x43300000, param15 ^ 0x80000000) - DOUBLE_8061a6f0);
                dvB = py;
                aveditor_draw_cell_icon(px, py, cell, color, param11, 0, param13, param14, param15, param16);
                dvA = px;
            }
        }
        aveditor_render_all_members();
    }
    return;
}

// Advance the active-piece highlight pulse animation: read the pulse mode from the piece record,
// derive the animated extent, and update sine/random phase state.
void aveditor_tick_pulse(void)
{
    float ext;
    float phase;
    int *piece;
    int mode;
    unsigned int kind;
    double base;

    piece = (int *)FUN_803826f0();
    mode = piece[1];
    ext = FLOAT_8061a70c;
    phase = FLOAT_8061a6e8;
    if (mode == 0) {
        kind = 0;
    }
    else if (mode == 2) {
        ext = *(float *)((int)piece + 0xc);
        kind = 2;
        phase = *(float *)((int)piece + 0x10);
    }
    else {
        kind = 0;
        if (mode < 2) {
            if (mode != 0 && -1 < mode) {
                kind = 3;
                ext = *(float *)((int)piece + 0xc);
                phase = FLOAT_8061a76c;
            }
        }
        else if (mode < 4) {
            ext = *(float *)((int)piece + 0xc);
            kind = 1;
            phase = *(float *)((int)piece + 0x10);
        }
    }
    base = (double)(float)((double)CONCAT44(0x43300000,
           (u32)(DAT_806108e4 * (int)(FLOAT_8061a770 * (FLOAT_8061a6e8 - ext))) ^ 0x80000000) -
           DOUBLE_8061a6f0);
    if (kind != 0) {
        if ((kind - 1 < 2) || (kind == 3)) {
            ext = (float)((double)CONCAT44(0x43300000, (u32)DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a6f0) / ext;
            int div = (int)ext;
            phase = phase + (float)((double)CONCAT44(0x43300000,
                    (u32)(DAT_80612028 - ((int)DAT_80612028 / div) * div) ^ 0x80000000) - DOUBLE_8061a6f0) / ext;
            for (; FLOAT_8061a6e8 <= phase; phase = phase - FLOAT_8061a6e8) {
            }
        }
        if (kind != 2) {
            if (kind < 2) {
                if (kind != 0) {
                    math_poly_trig((double)(FLOAT_8061a774 * phase - FLOAT_8061a778));
                }
            }
            else if ((kind < 4) && (*(int *)((int)piece + 8) <= (int)DAT_80612028)) {
                rng_counter_tick();
                *(int *)((int)piece + 8) =
                    (int)((double)(float)((double)CONCAT44(0x43300000, (u32)DAT_80612028 ^ 0x80000000) -
                    DOUBLE_8061a6f0) + base);
            }
        }
    }
    return;
}
