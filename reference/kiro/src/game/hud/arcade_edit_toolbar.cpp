// Batch: batch_893 | Module: arcade map-editor toolbar / button / label-anchor render
// Address range: 0x8031976c - 0x8031a25c
// Date: 2026-09-12
//
// The arcade map-editor's on-screen toolbar and cursor-box layer. It draws the four-way tool
// buttons (each a sprite quad hit-tested against the HUD cursor, with up to four directional hotkey
// labels), computes the pixel anchors for those labels/icons relative to a cell, and draws the
// selection cursor box as four grid lines. It sits on top of the text layer (text_* helpers), the
// sprite/GX layer, and the grid-line helper aveditor_draw_grid_line (batch < 892). Editor state
// lives in the context block at DAT_80612be4 (active tool at +0x228, cursor flags at +0x238).
//
//   aveditor_icon_anchor_below     0x8031976c  icon anchor, shifted up by the glyph height.
//   aveditor_text_anchor_right     0x80319824  right-aligned text label pixel anchor.
//   aveditor_text_anchor_center_x  0x8031990c  center a label in x (only when side==2 / centered).
//   aveditor_label_offset_for_side 0x80319978  pixel offset for a label given its side index (0..3).
//   aveditor_draw_toolbar_icon     0x803199e4  draw one tool icon + optional wrapped hotkey label.
//   aveditor_draw_button           0x80319c68  draw a button sprite (0x3f5), hit-test, fire callbacks.
//   aveditor_draw_button_with_labels 0x80319dcc draw a button then its four side hotkey labels.
//   aveditor_draw_label_row        0x80319f5c  draw a horizontal run of four stacked labels.
//   aveditor_draw_mode_button      0x8031a0d0  draw the active-tool mode button (enabled/disabled art).
//   aveditor_draw_cursor_box       0x8031a25c  draw the cursor selection box as four grid lines.
//
// NON-MATCHING notes:
//  * The compiler _savegpr_/_restgpr_ spill helpers (FUN_803826f0 / FUN_8038273c / __save_gpr_28 /
//    __restore_gpr_28 / FUN_803826d4 / FUN_803826d8 / FUN_803826e8 / FUN_80382720 / FUN_80382724 /
//    FUN_80382734 / FUN_80382654 / FUN_803826a0) are preserved as opaque calls. Where one folds the
//    incoming r3/r4 argument pair into an `undefined8` return, the real values are recovered as
//    locals through the >>0x20 / low-word split, matching the original ABI (an out-pointer pair for
//    the anchor helpers; a screen-record pointer for the button/mode helpers).
//  * The int<->float 0x43300000 / DOUBLE_8061a6f0 magic-double idiom is expressed with CONCAT44 from
//    common.h and plain (float)(int) casts.
//  * sprite_draw_icon and hud_screen_destroy_current are declared in generated_decls.h with a
//    narrower signature than the call site needs (sprite_draw_icon(int) vs the 3-arg call here;
//    hud_screen_destroy_current returns void but the original reads the current-screen record
//    pointer). To stay compile-clean without editing the shared header, both are reached through
//    thin file-local function-pointer wrappers with the real signature.
//  * The two dispatch tables (PTR_aveditor_text_anchor_from_cell_8047fc9c at 0x8047fc9c and
//    PTR_FUN_8047fc98 at 0x8047fc98) are arrays of function pointers indexed by (side + kind*8) /
//    tool; they are modeled as extern pointer-to-function arrays.
//
// Raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols get correctly-typed per-file externs below. Named helpers
// (text_*, sprite_get_texture_id, gx_*, hud_cursor_hit_test_rect, hud_screen_find_root,
// aveditor_draw_grid_line, aveditor_cell_query_flag, and this batch's own functions) resolve via
// the shared generated headers.

#include "common.h"
#include "hud.h"

extern "C" {

// -- editor context + layout constants --
extern int   DAT_80612be4;     // editor context base (active tool +0x228, cursor flags +0x238)
extern int*  DAT_80610a20;     // sprite material record table base (0x58 stride)
extern int   DAT_806119ec;     // active grid layout index
extern int   DAT_8051a820;     // grid layout table (screen-root handle, stride 0x1e0)
extern float DAT_8051a834;     // grid layout table X (stride 0x1e0)
extern float DAT_8051a838;     // grid layout table Y (stride 0x1e0)
extern int   DAT_80611654;     // text "measure width" flag
extern unsigned char DAT_80611a51; // cursor-box highlight alpha
extern int   DAT_80612c88;     // pixel origin x (magic-double)
extern int   DAT_80612c84;     // pixel origin y (magic-double)
extern float FLOAT_80612c58;   // inverse x scale
extern float FLOAT_80612c54;   // inverse y scale
extern float FLOAT_80612c78;   // world origin x
extern float FLOAT_80612c74;   // world origin y

// tool-button geometry tables
extern int   DAT_803e994c;     // tool-id lookup table (0xc stride, 4 entries)
extern int   DAT_8047fc8c;     // mode-button layout table (0x20 stride): x
extern int   DAT_8047fc90;     // mode-button layout: enabled art / y
extern int   DAT_8047fc94;     // mode-button layout: disabled art / y

// text anchor scale constants
extern float FLOAT_80611a64;   // text anchor x scale
extern float FLOAT_80611a68;   // icon anchor scale
extern float FLOAT_80611a6c;   // layout x bias
extern float FLOAT_80611a70;   // layout y bias
extern float FLOAT_8061a70c;   // 0.5 (half)
extern float FLOAT_8061a6e8;   // 1.0
extern float FLOAT_8061a7a8;   // text anchor x scale
extern float FLOAT_8061a7ac;   // text anchor y base
extern float FLOAT_8061a7b0;   // icon anchor scale
extern float FLOAT_8061a7b4;   // label side offset A
extern float FLOAT_8061a7b8;   // label side offset B
extern float FLOAT_8061a7bc;   // button label x inset
extern float FLOAT_8061a7c0;   // button label y inset
extern float FLOAT_8061a7c4;   // button label centering scale
extern float FLOAT_8061a7c8;   // label-row step
extern float FLOAT_8061a7cc;   // mode-button enabled scale
extern float FLOAT_8061a7d0;   // mode-button disabled scale
extern double DOUBLE_8061a6f0; // int->float conversion bias

// -- raw FUN_ helpers not yet individually named --
// text_word_wrap resolves via generated_externs.h (int text_word_wrap(...)); do not redeclare.
extern void FUN_80321550(void);                           // button primary action callback
extern void FUN_80321370(void);                           // button secondary action callback

// compiler _savegpr_/_restgpr_ spill helpers (opaque)
extern void FUN_803826f0(void);
extern void FUN_8038273c(void);
extern unsigned long long __save_gpr_28(void);   // folds an out-pointer pair
extern void __restore_gpr_28(void);
extern unsigned long long FUN_803826d4(void);    // folds screen-record pair
extern unsigned long long FUN_803826d8(void);    // folds x/y pair
extern unsigned long long FUN_803826e8(void);    // folds x/y pair
extern void FUN_80382720(void);
extern void FUN_80382724(void);
extern void FUN_80382734(void);
extern void FUN_80382654(void);
extern void FUN_803826a0(void);

// dispatch tables of function pointers (side/kind and per-tool)
extern void* PTR_aveditor_text_anchor_from_cell_8047fc9c[]; // [side + kind*8] label-anchor fns
extern void* PTR_FUN_8047fc98[];                            // [tool*8] mode-button draw fns
} // extern "C"

// sprite_draw_icon / hud_screen_destroy_current / aveditor_cell_query_flag have shared prototypes
// (generated_decls.h) that don't match the call sites here; reach them through function-pointer
// wrappers with the real signature. They resolve via the shared header, so no redeclaration needed.
static inline void __sprite_draw_icon3(unsigned int a, unsigned int b, int c)
{
    typedef void (*fn_t)(unsigned int, unsigned int, int);
    ((fn_t)(void*)&sprite_draw_icon)(a, b, c);
}
static inline int __editor_screen_record(void)
{
    typedef int (*fn_t)(void);
    return ((fn_t)(void*)&hud_screen_destroy_current)();
}
static inline int __editor_cell_query_flag(int member, int index)
{
    typedef int (*fn_t)(int, int);
    return ((fn_t)(void*)&aveditor_cell_query_flag)(member, index);
}

// -----------------------------------------------------------------------------
// Icon anchor variant that also shifts the y anchor up by the measured glyph height (used for
// labels placed above their cell). Writes (x,y) through the r3/r4 out-pointer pair.
void aveditor_icon_anchor_below(int outX, int outY, unsigned char* text, unsigned int row)
{
    double h;
    float iconScale = FLOAT_8061a7b0;
    float half = FLOAT_8061a70c;
    (void)outX; (void)outY;

    // FUN_803826f0 is the _savegpr_ spill; it folds the r3/r4 out-pointer pair into its return.
    typedef unsigned long long (*sp_t)(void);
    unsigned long long out = ((sp_t)(void*)&FUN_803826f0)();
    unsigned int* py = (unsigned int*)(unsigned int)out;
    int* px = (int*)(unsigned int)(out >> 0x20);

    h = text_get_char_height(text);
    double rowf = (double)(int)(row ^ 0x80000000) - DOUBLE_8061a6f0;
    *py = (int)((float)((double)(int)(*py ^ 0x80000000) - DOUBLE_8061a6f0) -
                (float)((double)FLOAT_8061a7a8 * (double)FLOAT_80611a64 + h));
    *px = (int)(iconScale * FLOAT_80611a68 * half + (float)rowf);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Right-aligned text label anchor: the x anchor is pushed left by the measured run width (halved
// when the measure-width flag is set). Writes (x,y) through the r3/r4 out-pointer pair.
void aveditor_text_anchor_right(int outX, int outY, unsigned char* text, unsigned int row)
{
    unsigned long long out;
    double w;
    (void)outX; (void)outY;

    out = __save_gpr_28();
    int* px = (int*)(unsigned int)(out >> 0x20);
    unsigned int* py = (unsigned int*)(unsigned int)out;

    if (DAT_80611654 == 1) {
        w = (double)(float)((double)text_get_char_width(text) * (double)FLOAT_8061a70c);
    } else {
        w = (double)text_get_char_width(text);
    }
    *px = (int)-(FLOAT_8061a7a8 * FLOAT_80611a64 -
                 (float)((double)(float)((double)(int)(row ^ 0x80000000) - DOUBLE_8061a6f0) - w));
    double h = text_get_char_height(text);
    *py = (int)(FLOAT_8061a70c * (float)((double)FLOAT_8061a7ac - h) +
                (float)((double)(int)(*py ^ 0x80000000) - DOUBLE_8061a6f0));
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// Center a label in x (only for side==2 = centered): shift x left by the measured width. out points
// at the destination x word; row/side supplied.
void aveditor_text_anchor_center_x(unsigned int* outX, int unused, unsigned char* text, int u4, int side)
{
    (void)unused; (void)u4;
    if (side == 2) {
        double w = text_get_char_width(text);
        *outX = (int)((double)(float)((double)(int)(*outX ^ 0x80000000) - DOUBLE_8061a6f0) - w);
    }
}

// -----------------------------------------------------------------------------
// Pixel offset for a label given its side index. Sides 0 and 3 use offset B; sides 1 and 2 use
// offset A; out-of-range returns 0.
int aveditor_label_offset_for_side(int side)
{
    if (side != 3) {
        if (2 < side) return 0;
        if (side != 0) {
            if (side < 0) return 0;
            return (int)(FLOAT_8061a7b4 * FLOAT_80611a68 + FLOAT_80611a70);
        }
    }
    return (int)(FLOAT_8061a7b8 * FLOAT_80611a68 + FLOAT_80611a70);
}

// -----------------------------------------------------------------------------
// Draw one tool icon (sprite_draw_icon) and, if the tool cell is populated and its query flag is
// set, the wrapped hotkey label under it. tool is the tool id; iconIndex/x/y position it; side
// selects the label anchor. Reads the tool table at 0x803e994c and the per-cell record at r4[+0x124].
void aveditor_draw_toolbar_icon(int tool, int side, unsigned int x, int iconIndex, unsigned int y, int u6)
{
    unsigned long long spill;
    int recBase, toolSlot, iVar12;
    int cellPop = 0, cellFlag = 0;

    spill = FUN_803826d4();
    recBase = (int)(spill >> 0x20);      // per-cell record base (r4)
    int toolId = (int)spill;             // resolved tool id (r3)

    // resolve the tool index by scanning the 4-entry tool table
    toolSlot = 0; iVar12 = 4;
    for (int off = 0; toolId != *(int*)((int)&DAT_803e994c + off); off = off + 0x0c) {
        toolSlot = toolSlot + 1;
        iVar12 = iVar12 - 1;
        if (iVar12 == 0) { FUN_80382720(); return; }
    }

    // find the populated cell whose slot matches this tool
    for (int idx = 0, o = 0; idx < 4; idx = idx + 1, o = o + 0x10) {
        int rec = recBase + o;
        if (*(int*)(rec + 0x124) == toolSlot) {
            cellPop = *(int*)(rec + 0x128);
            cellFlag = __editor_cell_query_flag(recBase, idx);
        }
    }

    unsigned int savedX = x;
    __sprite_draw_icon3((unsigned int)iconIndex, savedX, toolId);
    int savedFlag = DAT_80611654;
    DAT_80611654 = savedFlag;

    if ((cellPop != 0) && (cellFlag != 0)) {
        unsigned char buf[0x40];
        unsigned int labelY = y;
        int fontPrev = text_set_font_table(0x12);
        int wrapPrev = text_set_wrap_mode(0);
        unsigned int width = (unsigned int)aveditor_label_offset_for_side(toolId);
        memset_wrapper((int)(intptr_t)buf, 0, 0x40);
        DAT_80611654 = 1;
        text_word_wrap();
        double w = text_get_char_width(buf);
        bool clip = (double)(float)((double)(int)(width ^ 0x80000000) - DOUBLE_8061a6f0) < w;
        if (clip) {
            text_apply_alpha((double)(float)((double)(int)(width ^ 0x80000000) - DOUBLE_8061a6f0), buf, 0);
        }
        text_get_char_height(buf);
        text_set_wrap_mode(wrapPrev);

        void* fn = PTR_aveditor_text_anchor_from_cell_8047fc9c[toolId + cellFlag * 8];
        if (fn != 0) {
            typedef void (*anchor_t)(unsigned int*, unsigned int*, unsigned char*, int, int);
            ((anchor_t)fn)(&labelY, &savedX, buf, iconIndex, u6);
        }
        text_draw_char_shadowed(
            (double)(float)((double)(int)(labelY ^ 0x80000000) - DOUBLE_8061a6f0),
            (double)(float)((double)(int)(savedX ^ 0x80000000) - DOUBLE_8061a6f0),
            buf, 0x64646480, 0x80);
        if (clip) {
            double one = (double)FLOAT_8061a6e8;
            text_set_color_rgb(one, one, one);
        }
        text_set_font_table(fontPrev);
        DAT_80611654 = savedFlag;
    }
    FUN_80382720();
}

// -----------------------------------------------------------------------------
// Draw a button sprite (icon 0x3f5) at (x0,y0)-(x1,y1), hit-test it against the HUD cursor, set the
// pressed flag in the cursor state (+0x238) and, on hit, fire the primary (and conditionally the
// secondary) callback. cb is the primary callback; the secondary arrives folded in r4.
void aveditor_draw_button(unsigned long long btn_pair, double x0, double y0, double x1,
                          int u5, int u6, void* cb)
{
    int texId, hit;
    void* cb2;
    double bx0;
    float rect[4];
    (void)btn_pair; (void)u5; (void)u6;

    FUN_80382654();
    unsigned long long spill = __save_gpr_28();
    bx0 = (double)spill;                 // button x0 (folded)
    cb2 = (void*)(unsigned int)(spill >> 0x20); // secondary callback (r4)

    texId = sprite_get_texture_id(0x3f5);
    if ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x400) == 0) {
        gx_set_active_material(texId);
        rect[0] = (float)bx0; rect[1] = (float)x0; rect[2] = (float)y0; rect[3] = (float)x1;
        hit = hud_cursor_hit_test_rect(rect);
        if (hit == 0) *(unsigned int*)(DAT_80612be4 + 0x238) &= 0xfffffdff;
        else          *(unsigned int*)(DAT_80612be4 + 0x238) |= 0x200;

        int rec = *DAT_80610a20 + texId * 0x58;
        gx_draw_sprite_quad(bx0, x0, y0, x1, 0, 0,
                            (unsigned int)*(unsigned short*)(rec + 0x4e) << 4,
                            (unsigned int)*(unsigned short*)(rec + 0x52) << 4);
        if (hit != 0) {
            if (cb != 0) ((void(*)(void))cb)();
            if (cb2 != 0) {
                int root = hud_screen_find_root(*(int*)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(unsigned int*)(root + 0xc0) & 0x10) != 0) ((void(*)(void))cb2)();
            }
        }
    }
    FUN_803826a0();
    __restore_gpr_28();
}

// -----------------------------------------------------------------------------
// Draw a button then its four directional hotkey labels (top / bottom / left / right), each via
// aveditor_draw_toolbar_icon at the button-relative offset.
void aveditor_draw_button_with_labels(int u1, int u2, unsigned int x, unsigned int flag, int u5)
{
    unsigned long long spill;
    (void)u1; (void)u2;

    spill = FUN_803826d8();
    int screen = (int)(spill >> 0x20);   // screen record (r4)
    unsigned int y = (unsigned int)spill;

    unsigned int dx = (unsigned int)(FLOAT_8061a7bc * FLOAT_80611a68);
    unsigned int dy = (unsigned int)(FLOAT_8061a7c0 * FLOAT_80611a64);
    unsigned int cx = x + dx;
    unsigned int cy = y + dy;

    aveditor_draw_button(0,
        (double)(float)((double)(int)((cx) ^ 0x80000000) - DOUBLE_8061a6f0),
        (double)(float)((double)(int)((cy) ^ 0x80000000) - DOUBLE_8061a6f0),
        (double)(FLOAT_8061a7c4 * (float)((double)(int)(dx ^ 0x80000000) - DOUBLE_8061a6f0) +
                (float)((double)(int)(x ^ 0x80000000) - DOUBLE_8061a6f0)),
        (int)(FLOAT_8061a7c4 * (float)((double)(int)(dy ^ 0x80000000) - DOUBLE_8061a6f0) +
              (float)((double)(int)(y ^ 0x80000000) - DOUBLE_8061a6f0)),
        screen, (void*)FUN_80321550);
    (void)FUN_80321370;

    aveditor_draw_toolbar_icon(screen, 2, y, (int)cx, flag, u5);
    aveditor_draw_toolbar_icon(screen, 0, cy, (int)(x + dx * 2), flag, u5);
    aveditor_draw_toolbar_icon(screen, 3, cy, (int)x, flag, u5);
    aveditor_draw_toolbar_icon(screen, 1, y + dy * 2, (int)cx, flag, u5);
    FUN_80382724();
}

// -----------------------------------------------------------------------------
// Draw a horizontal run of four stacked labels, each stepped along by FLOAT_8061a7c8.
void aveditor_draw_label_row(int u1, int u2, int u3, unsigned int flag, int u5)
{
    unsigned long long spill;
    (void)u1; (void)u2;

    spill = FUN_803826e8();
    int screen = (int)(spill >> 0x20);
    unsigned int base = (unsigned int)spill;

    unsigned int p = (unsigned int)(FLOAT_8061a7c8 * FLOAT_80611a64 +
                     (float)((double)(int)(base ^ 0x80000000) - DOUBLE_8061a6f0));
    aveditor_draw_toolbar_icon(screen, 2, p, u3, flag, u5);
    p = (unsigned int)(FLOAT_8061a7c8 * FLOAT_80611a64 + (float)((double)(int)(p ^ 0x80000000) - DOUBLE_8061a6f0));
    aveditor_draw_toolbar_icon(screen, 0, p, u3, flag, u5);
    p = (unsigned int)(FLOAT_8061a7c8 * FLOAT_80611a64 + (float)((double)(int)(p ^ 0x80000000) - DOUBLE_8061a6f0));
    aveditor_draw_toolbar_icon(screen, 3, p, u3, flag, u5);
    aveditor_draw_toolbar_icon(screen, 1,
        (int)(FLOAT_8061a7c8 * FLOAT_80611a64 + (float)((double)(int)(p ^ 0x80000000) - DOUBLE_8061a6f0)),
        u3, flag, u5);
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// Draw the active-tool mode button. Chooses the enabled or disabled art depending on the current
// screen's flags and the cursor pressed state, then dispatches to the per-tool draw fn from the
// PTR_FUN_8047fc98 table (indexed by the active tool at +0x228).
void aveditor_draw_mode_button(void)
{
    unsigned long long spill;
    int enabled, layoutOff, art;
    float scale;

    typedef unsigned long long (*sp_t)(void);
    spill = ((sp_t)(void*)&FUN_803826f0)();  // _savegpr_ spill folds r3 (draw arg) into its return
    unsigned int r3 = (unsigned int)spill;
    enabled = 2;
    int fontPrev = text_set_font_table(0x12);
    int screen = __editor_screen_record();
    if ((*(unsigned int*)(screen + 0x40) & 0x800) == 0) {
        if (((*(unsigned int*)(screen + 0x214) & 2) == 0) &&
            ((*(unsigned int*)(DAT_80612be4 + 0x238) & 0x100) == 0))
            goto have_state;
    }
    enabled = 0;
have_state:
    layoutOff = *(int*)(DAT_80612be4 + 0x228) * 0x20;
    double xf = (double)(int)(*(int*)(&DAT_8047fc8c + layoutOff) ^ 0x80000000);
    if (enabled == 0) { art = *(int*)(&DAT_8047fc90 + layoutOff); scale = FLOAT_8061a7cc; }
    else              { art = *(int*)(&DAT_8047fc94 + layoutOff); scale = FLOAT_8061a7d0; }

    if ((*(unsigned int*)(screen + 0x214) & 0x10) == 0) {
        void* fn = PTR_FUN_8047fc98[*(int*)(DAT_80612be4 + 0x228) * 8];
        typedef void (*draw_t)(unsigned int, int, int, int, int);
        ((draw_t)fn)(r3,
            (int)(FLOAT_80611a64 * (float)(xf - DOUBLE_8061a6f0) + FLOAT_80611a6c),
            (int)(FLOAT_80611a68 * (float)((double)(int)(art ^ 0x80000000) - DOUBLE_8061a6f0) + FLOAT_80611a70),
            (int)(scale * FLOAT_80611a68 + FLOAT_80611a70), enabled);
    }
    text_set_font_table(fontPrev);
    FUN_8038273c();
}

// -----------------------------------------------------------------------------
// Draw the cursor selection box as four grid lines around the cursor rect (world coords at
// +0x2f94/+0x2f98, extent from the grid-layout table), tinted yellow with the highlight alpha.
void aveditor_draw_cursor_box(void)
{
    double x1 = (double)(FLOAT_80612c58 *
                (*(float*)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
                 (float)((double)(int)(DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a6f0)) + FLOAT_80612c78);
    unsigned int color = DAT_80611a51 | 0x7f7f0000;
    double y1 = (double)(FLOAT_80612c54 *
                (*(float*)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
                 (float)((double)(int)(DAT_80612c84 ^ 0x80000000) - DOUBLE_8061a6f0)) + FLOAT_80612c74);
    double cx = (double)*(float*)(DAT_80612be4 + 0x2f94);
    double cy = (double)*(float*)(DAT_80612be4 + 0x2f98);
    aveditor_draw_grid_line(cx, cy, x1, cy, color);
    aveditor_draw_grid_line(cx, cy, cx, y1, color);
    aveditor_draw_grid_line(x1, cy, x1, y1, color);
    aveditor_draw_grid_line(cx, y1, x1, y1, color);
}
