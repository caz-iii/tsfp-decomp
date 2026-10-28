// boss3_render.cpp - the third segmented boss's render pass (control block
// DAT_80612a48; see boss3_think.cpp / boss3_tentacle.cpp / boss3_surface.cpp /
// boss3_control.cpp / boss3_combat.cpp for the rest of the family).
//
//   * boss3_render_part_shadow projects one boss part's shadow/decal: it hands
//     the part's world transform (base+0x40) and a source point (base+0x24) plus
//     the level's active projection matrix (*(DAT_80611d54+0x208)) to the shared
//     skin/matrix helper FUN_802b43e4.
//   * boss3_render is the render entry. When the boss is alive and in a visible
//     state (control+0x98 == 1 or 2) it emits the body in two material sub-passes
//     (opaque id 1, then the additive/decal id 2), draws the core mesh via
//     boss3_render (the model draw at FUN_802b01e4 / FUN_802aeee0 / boss3_render),
//     projects the shell decal, and then splats the four tentacle-tip shadows
//     whose "alive" timer (control+0x194 / +0x2c0 / +0x524 / +0x4c4) still exceeds
//     the visibility threshold FLOAT_80619568.
//
// Address range: 0x802a45b8 - 0x802a460b
// Subsystem: game (boss AI / render)

#include "common.h"

extern "C" {

// --- data globals ----------------------------------------------------------
extern int   DAT_80612a48;   // boss3 control block base (byte-addressed)
extern int   DAT_80611d54;   // level/render state base (+0x208 = active proj matrix ptr)

extern const float FLOAT_80619568;   // tentacle-tip visibility threshold

// --- helpers (raw, resolved elsewhere in the tree) --------------------------
extern void FUN_802b43e4(int worldXform, int srcPoint, float* projMatrix); // skin/decal xform
extern void FUN_802b01e4(void);          // boss3 model draw, sub-pass A
extern void FUN_802aeee0(void);          // boss3 model draw, sub-pass B
extern void boss3_render(void);          // core mesh draw at 0x8029eb58

extern void gx_material_state_nop(void); // gx_material_state_nop()
extern void gx_set_sub_material_id(int id);

} // extern "C"

// forward decls
void boss3_render_part_shadow(int partBase); // 0x802a45b8
void boss3_render_pass(void);                 // 0x802a460c (Ghidra: boss3_render_pass)

// ---------------------------------------------------------------------------
// 0x802a45b8 - project one boss part's shadow/decal through the shared skin
// transform, using the level's active projection matrix.
void boss3_render_part_shadow(int partBase)
{
    FUN_802b43e4(partBase + 0x40, partBase + 0x24,
                 *(float**)(DAT_80611d54 + 0x208));
}

// ---------------------------------------------------------------------------
// 0x802a460c - boss3 render entry. Only draws while the boss is present and in
// a visible state (control+0x98 in {1,2}).
void boss3_render_pass(void)
{
    int ctrl = DAT_80612a48;
    if (ctrl == 0)
        return;

    char state = *(char*)(ctrl + 0x98);
    if (state != '\x01' && state != '\x02')
        return;

    gx_material_state_nop();

    gx_set_sub_material_id(2);
    FUN_802b01e4();

    gx_set_sub_material_id(1);
    FUN_802aeee0();
    boss3_render();

    // shell decal
    gx_set_sub_material_id(2);
    FUN_802b43e4(ctrl + 0x554, ctrl + 0x574,
                 *(float**)(DAT_80611d54 + 0x208));

    // four tentacle-tip shadows, each gated on its own alive timer
    gx_set_sub_material_id(1);
    if (FLOAT_80619568 < *(float*)(ctrl + 0x194))
        boss3_render_part_shadow(ctrl + 0x164);
    if (FLOAT_80619568 < *(float*)(ctrl + 0x2c0))
        boss3_render_part_shadow(ctrl + 0x290);
    if (FLOAT_80619568 < *(float*)(ctrl + 0x524))
        boss3_render_part_shadow(ctrl + 0x4f4);
    if (FLOAT_80619568 < *(float*)(ctrl + 0x4c4))
        boss3_render_part_shadow(ctrl + 0x494);
}
