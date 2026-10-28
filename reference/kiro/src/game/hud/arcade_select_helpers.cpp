// Batch: batch_837
// Arcade-select screen general helpers: center logo draw, split-mode query,
// row scale + menu column-count / x-offset layout math.
//
// These are screen-level helpers for the arcade character-select screen,
// distinct from the ticker widget internals in arcade_select_ticker.cpp.

#include "common.h"

// ---- Per-file externs for raw symbols referenced by these functions ----
extern "C" {
    // GPR save/restore helpers emitted by the CW prologue.
    int  __save_gpr_28(void);
    void __restore_gpr_28(void);

    // Sprite / material / draw helpers (named cross-module; but referenced
    // here with these exact signatures for the local call sites).
    int  sprite_get_texture_id(int id);
    void gx_set_sub_material_id(int id);
    void gx_set_active_material(int material);
    void gx_draw_sprite_quad(double x0, double y0, double x1, double y1,
                             int u0, int v0, unsigned u1, unsigned v1);

    // Screen-state / player / session queries.
    int camera_get_split_mask(void);
    int player_get_active_count(void);
    int game_is_standard_arcade(void);
    int session_get_state_flag(void);

    // Raw function referenced by the logo draw (sub-material select math).
    void FUN_803825c0(double v);

    // Raw data symbols.
    extern char         DAT_805015bd;
    extern unsigned int DAT_805015c0;
    extern int          DAT_80612610;
    extern int          DAT_80611934;
    extern int          DAT_80612bc8;
    extern int          DAT_80611748;   // base address value; +0x20 dereferenced
    extern int          DAT_806109c4;
    extern int          DAT_806109c8;
    extern int         *DAT_80610a20;

    extern float  FLOAT_8060f958;
    extern float  FLOAT_8061a3d0;
    extern float  FLOAT_8061a414;
    extern float  FLOAT_8061a418;
    extern float  FLOAT_8061a41c;
    extern float  FLOAT_8061a420;
    extern float  FLOAT_8061a424;
    extern float  FLOAT_8061a428;
    extern float  FLOAT_8061a42c;

    extern double DOUBLE_8061a368;
    extern double DOUBLE_8061a430;
}

// u32->float via the GameCube 0x43300000 CONCAT44 magic-double idiom.
// Kept literal (bias differs per call site) rather than collapsing to a cast.
static inline double u32_bias(unsigned x, double bias)
{
    return (double)CONCAT44(0x43300000, x ^ 0x80000000) - bias;
}

// Address: 0x802ee5d4 | Name: arcsel_draw_center_logo
// Draws the arcade-select center logo (sprite 0x29a body + 0x29b glow),
// centered from the on-screen extents DAT_806109c4/c8, scaled by param_3.
void arcsel_draw_center_logo(u64 param_1, double param_2, double param_3)
{
    (void)param_1;
    double saveBase = (double)__save_gpr_28();

    sprite_get_texture_id(0x29b);

    // Half-extents in screen units.
    unsigned halfW = (unsigned)((double)((float)(u32_bias((unsigned)DAT_806109c4, DOUBLE_8061a368)
                                                 / FLOAT_8061a414)) * (double)FLOAT_8060f958);
    unsigned halfH = (unsigned)((float)(u32_bias((unsigned)-DAT_806109c8, DOUBLE_8061a368)
                                        / FLOAT_8061a414));

    double cx = -(double)(float)((double)(float)(u32_bias((unsigned)((int)DAT_806109c4 / 10),
                                                          DOUBLE_8061a368) * (double)FLOAT_8060f958)
                                 - saveBase);
    double cy = (double)(float)(param_2 + (double)(float)u32_bias(
                    (unsigned)(DAT_806109c8 / 7 + 10), DOUBLE_8061a368));

    gx_set_sub_material_id(1);
    int mat = sprite_get_texture_id(0x29a);
    gx_set_active_material(mat);

    int frameBase = *DAT_80610a20;
    FUN_803825c0((double)(float)((double)FLOAT_8061a3d0 * param_3));

    unsigned hw = (unsigned)(((int)halfW >> 1) + (int)((int)halfW < 0 && (halfW & 1) != 0)) ^ 0x80000000u;
    unsigned hh = (unsigned)(((int)halfH >> 1) + (int)((int)halfH < 0 && (halfH & 1) != 0)) ^ 0x80000000u;

    frameBase = frameBase + mat * 0x58;

    double dw = (double)(float)u32_bias(hw, DOUBLE_8061a368);
    double dh = (double)(float)u32_bias(hh, DOUBLE_8061a368);

    gx_draw_sprite_quad((double)(float)(cx - dw),
                        (double)(float)(cy - dh),
                        (double)(float)(cx + dw),
                        (double)(float)(cy + dh),
                        0, 0,
                        (unsigned)(*(unsigned short *)(frameBase + 0x4e)) << 4,
                        (unsigned)(*(unsigned short *)(frameBase + 0x52)) << 4);

    __restore_gpr_28();
}

// Address: 0x802ee7bc | Name: arcsel_is_10player_split
// True when in the 10-player ('\n') split layout: more than one active view
// AND either the camera reports a split mask or state flag 0x200 is set.
u32 arcsel_is_10player_split(void)
{
    if (DAT_805015bd == '\n' && 1 < DAT_80612610) {
        int mask = camera_get_split_mask();
        if (mask != 0 || (DAT_805015c0 & 0x200) != 0) {
            return 1;
        }
    }
    return 0;
}

// Address: 0x802ee824 | Name: arcsel_get_row_scale
// Picks the per-row vertical scale for the arcade-select grid.
double arcsel_get_row_scale(void)
{
    if (player_get_active_count() == 9) {
        return (double)FLOAT_8061a418;
    }
    if (DAT_80611934 == 9) {
        if (game_is_standard_arcade() == 0) {
            return (double)FLOAT_8061a420;
        }
        return (double)FLOAT_8061a41c;
    }
    return (double)FLOAT_8061a418;
}

// Address: 0x802ee880 | Name: arcsel_menu_column_count
// Column count for the arcade-select menu region.
//   param==0 -> 1 column
//   param==1 -> 1 when session flag < 4 (clz(4-flag)>>5 == 1 iff 4-flag==0? ),
//               otherwise 0 (branch-free clz idiom from the original)
//   else     -> 0
uint arcsel_menu_column_count(int param_1)
{
    if (param_1 == 0) {
        return 1;
    }
    if (param_1 == 1) {
        int flag = session_get_state_flag();
        uint clz = (uint)countLeadingZeros(4 - flag);
        return clz >> 5;
    }
    return 0;
}

// Address: 0x802ee8cc | Name: arcsel_menu_x_offset
// Horizontal offset for the arcade-select menu column, scaled from the
// value at *(DAT_80611748 + 0x20). Uses a wider scale in the single-column
// (flag<2 or DAT_80612bc8==0) case, else column-count dependent scale.
double arcsel_menu_x_offset(int param_1)
{
    unsigned raw = *(unsigned *)(DAT_80611748 + 0x20);
    double scaled = (double)(float)u32_bias(raw, DOUBLE_8061a430);

    if (session_get_state_flag() < 2 || DAT_80612bc8 == 0) {
        return (double)(FLOAT_8061a42c * (float)scaled);
    }
    if (arcsel_menu_column_count(param_1) == 0) {
        return (double)(FLOAT_8061a428 * (float)scaled);
    }
    return (double)(FLOAT_8061a424 * (float)scaled);
}
