// =============================================================================
// hud_pulsing_box.cpp - HUD pulsing selection box (batch 796).
//
//   - hud_draw_pulsing_box (0x802c7534): draw a 4-line rectangle outline whose
//     colour brightness pulses over time. The pulse phase is read from the shared
//     sine/cosine table (DAT_80583228) indexed by the global 60-frame counter
//     (DAT_80612020 % 0x3c), scaled into a grey level that is packed into the R/G/B
//     channels (alpha fixed 0x7f). The rectangle corners come from the frame-helper
//     register pair (left/right x) plus the two y parameters. Used to highlight the
//     selected weapon/menu cell in the HUD.
//
// Address: 0x802c7534 (single function)
// Subsystem: graphics (HUD line draw)
//
// NON-MATCHING: the PPC frame register-save stub (FUN_803826e8) supplied the two
// x coordinates (left/right); expressed here as explicit x0/x1 parameters. The
// CONCAT44 int->double bias for the frame-counter int->float is reduced to a plain
// (float)(int) cast.
// =============================================================================

#include "common.h"

extern "C" {

// shared 2048-entry sin/cos lookup table (index masked to 0x7ff)
extern const float DAT_80583228;
// global free-running frame counter
extern int DAT_80612020;

extern void gx_draw_line_i(int x0, int y0, int x1, int y1, unsigned int colour);

// tuning pool
extern const float FLOAT_80619ee8;   // brightness base
extern const float FLOAT_80619eec;   // table index/base offset
extern const float FLOAT_80619ef0;   // phase scale
extern const float FLOAT_80619ef4;   // counter scale
extern const float FLOAT_80619ef8;   // counter divisor
extern const float FLOAT_80619efc;   // brightness amplitude
extern const double DOUBLE_80619f00; // 2^52 int->double bias

// -----------------------------------------------------------------------------
// hud_draw_pulsing_box (0x802c7534)
// x0/x1 = left/right (from the frame-helper register pair), y0/y1 = param y's.
// -----------------------------------------------------------------------------
void hud_draw_pulsing_box(int x0, int x1, int y0, int y1)
{
    // pulse phase from the 60-frame counter through the sin/cos table
    int tblIdx = (int)(FLOAT_80619ef0 *
        ((FLOAT_80619ef4 *
          (float)((float)(int)((DAT_80612020 % 0x3c) ^ 0x80000000) - (float)DOUBLE_80619f00)) /
         FLOAT_80619ef8) + FLOAT_80619eec);
    unsigned int level = (unsigned int)(FLOAT_80619efc *
        (FLOAT_80619eec * (&DAT_80583228)[tblIdx & 0x7ff] + FLOAT_80619ee8));

    unsigned int colour = (level & 0xff) << 8 | level << 0x18 | (level & 0xff) << 0x10 | 0x7f;

    gx_draw_line_i(x0, y0, x1, y0, colour);   // top
    gx_draw_line_i(x0, y1, x1, y1, colour);   // bottom
    gx_draw_line_i(x0, y0, x0, y1, colour);   // left
    gx_draw_line_i(x1, y0, x1, y1, colour);   // right
}

} // extern "C"
