// Batch: batch_860 | Module: arcade-select results/scoreboard screen initializers
// Address range: 0x802fff6c - 0x803004c8
//
// The HUD screen-init entry points for the arcade-select results and scoreboard
// screens, plus their small confirm/push gates and a layout-index helper.
//
// arcsel_scoreboard_screen_init lays out the scoreboard element: it runs the
// scoreboard panel once to measure width, optionally biases the layout toward the
// confirm side (arcsel_mode_needs_confirm), installs the auto-advance callbacks at
// obj+0x1c0/0x1c4, initialises the element, adds the scoreboard spacer item, ticks
// the scroll, and registers the two global button handlers. arcsel_results_detail_screen_init
// and arcsel_results_screen_init_variant build the per-player results element
// (detail panel + weapon-icon strip spacers), the latter picking taller layouts
// when the session state flag is negative and centring via
// arcsel_layout_scoreboard_center. arcsel_results_init_cb1..4 are the identical
// hud trampolines into the variant initialiser.
//
// arcsel_scoreboard_confirm_or_push / arcsel_results_push_gate are the confirm
// buttons: they branch on arcsel_show_teams_prompt then push the next screen.
// arcsel_results_layout_index maps a HUD element's layout vtable pointer (obj+4)
// to a 0-3 index by scanning the PTR_FUN_804777f0 table (0x10 stride, 4 entries).
//
// Named cross-module helpers (hud_*, arcsel_*, get_string_by_id,
// session_get_state_flag) resolve through the shared generated headers; only raw
// FUN_/DAT_/FLOAT_/PTR_ symbols get per-file externs.

#include "common.h"
#include "hud.h"

// ---- raw symbols referenced by this translation unit -------------------------
extern "C" {
u64  FUN_803826f0(void);            // hud active-screen ctx (pair)
void FUN_80300634(void);            // hud: push scoreboard sub-screen
void FUN_8038273c(void);            // hud screen finalize

extern float FLOAT_80611a5c;
extern float FLOAT_8061a488;
extern float FLOAT_8061a4f8;
extern float FLOAT_8061a538;
extern float FLOAT_8061a5a0;
extern float FLOAT_8061a5d0;
extern float FLOAT_8061a5d4;
extern float FLOAT_8061a5dc;

extern int   DAT_803e0a28;   // scoreboard-init layout template
extern int   DAT_803e0a2c;
extern int   DAT_803e0a30;
extern int   DAT_803e0a34;
extern int   DAT_806119e0;   // results element center anchor
extern int   PTR_FUN_804777f0; // layout vtable table base (0x10 stride, 4 entries)
}

// ============================================================================
// 0x803004c8  arcsel_results_layout_index
// Map a HUD element's layout vtable pointer (obj+4) to a 0-3 index by scanning
// the 4-entry PTR_FUN_804777f0 table (0x10 byte stride); returns 0 if unmatched.
// ============================================================================
int arcsel_results_layout_index(int obj)
{
    int idx = 0;
    int off = 0;
    int n = 4;
    do {
        if (*(int*)(obj + 4) == (int)&PTR_FUN_804777f0 + off) {
            return idx;
        }
        idx = idx + 1;
        off = off + 0x10;
        n = n + -1;
    } while (n != 0);
    return 0;
}

// ============================================================================
// 0x802fff6c  arcsel_scoreboard_confirm_or_push
// Confirm button: if the teams prompt is not required, push the scoreboard
// sub-screen (FUN_80300634); otherwise push the current screen.
// ============================================================================
void arcsel_scoreboard_confirm_or_push(void)
{
    int iVar1 = arcsel_show_teams_prompt();
    if (iVar1 == 0) {
        FUN_80300634();
    } else {
        hud_screen_push();
    }
}

// ============================================================================
// 0x80300230  arcsel_results_push_gate
// Confirm button: both branches push the next screen (the teams-prompt check is
// retained for parity with the original control flow).
// ============================================================================
void arcsel_results_push_gate(void)
{
    int iVar1 = arcsel_show_teams_prompt();
    if (iVar1 == 0) {
        hud_screen_push();
    } else {
        hud_screen_push();
    }
}

// ============================================================================
// 0x803000bc  arcsel_scoreboard_screen_init
// Build the scoreboard HUD element: measure it via the panel, optionally bias the
// layout toward the confirm side, install auto-advance callbacks, init the element,
// add the scoreboard spacer, tick the scroll and register the two button handlers.
// ============================================================================
void arcsel_scoreboard_screen_init(int obj)
{
    int iVar1;
    double dVar3, dVar4, dVar5;
    float local_38;
    uint local_34, local_30;
    u32 local_2c;

    local_38 = (float)DAT_803e0a28;
    local_34 = DAT_803e0a2c;
    local_30 = DAT_803e0a30;
    local_2c = DAT_803e0a34;
    arcsel_draw_scoreboard_panel((u32)(uintptr_t)&local_38, 0, &local_38, 0, DAT_803e0a30,
                                 DAT_803e0a2c, (uint)DAT_803e0a28, (u32)(uintptr_t)&DAT_803e0a28);
    dVar3 = (double)local_38;   // measured element width (layout width slot)
    dVar4 = arcsel_scoreboard_x_pos();
    uint uVar2 = 0;
    dVar5 = dVar3;
    if ((dVar4 < dVar3) && (iVar1 = arcsel_mode_needs_confirm(), dVar5 = dVar4, iVar1 != 0)) {
        uVar2 = 0x40000000;
        dVar5 = (double)(float)((double)FLOAT_8061a5d0 * (double)(float)(dVar3 - dVar4) + dVar4);
    }
    *(void**)(obj + 0x1c0) = (void*)&arcsel_auto_advance_if_ready;
    *(void**)(obj + 0x1c4) = (void*)&arcsel_auto_advance_next;
    hud_element_init_from_layout((int)(uVar2 | 0x2020c), 0,
                                 (int)(uintptr_t)&arcsel_scoreboard_push_screen,
                                 (int)(uintptr_t)&arcsel_scoreboard_confirm_or_push);
    hud_add_spacer_item((float)(dVar5 * (double)FLOAT_80611a5c), 0,
                        (int)(uintptr_t)&arcsel_scoreboard_render_cb);
    arcsel_element_tick_scroll(obj);
    iVar1 = get_string_by_id(0xb02);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
    iVar1 = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, iVar1, 0, 0);
    if (*(float*)(obj + 0x60) < FLOAT_8061a488) {
        *(float*)(obj + 0x60) = FLOAT_8061a488;
    }
    *(float*)(obj + 0x20c) = FLOAT_8061a4f8;
    (void)local_34; (void)local_30; (void)local_2c;
}

// ============================================================================
// 0x8030028c  arcsel_results_detail_screen_init
// Build the per-player results element: detail panel + weapon-icon strip spacers,
// centred; installs the confirm gate and one button handler.
// ============================================================================
void arcsel_results_detail_screen_init(int obj)
{
    double dVar2;

    hud_element_init_from_layout(0, 0, (int)(uintptr_t)&arcsel_results_push_gate, 0);
    dVar2 = (double)FLOAT_8061a4f8;
    *(void**)(obj + 0x1c0) = (void*)&arcsel_auto_advance_if_ready;
    *(void**)(obj + 0x1c4) = (void*)&arcsel_auto_advance_next;
    hud_set_item_padding_mode((float)dVar2, 1, 1, 1);
    hud_add_spacer_item(FLOAT_8061a5a0, 0, (int)(uintptr_t)&arcsel_draw_results_detail);
    hud_add_spacer_item(FLOAT_8061a5d4, 0, (int)(uintptr_t)&arcsel_draw_weapon_icons);
    hud_element_apply_center_xy(&DAT_806119e0);
    *(float*)(obj + 0x20c) = FLOAT_8061a4f8;
    int iVar1 = get_string_by_id(0xb02);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
}

// ============================================================================
// 0x80300350  arcsel_results_screen_init_variant
// Results element init variant: taller detail/icon spacer heights when the session
// state flag is negative, centred via arcsel_layout_scoreboard_center.
// ============================================================================
void arcsel_results_screen_init_variant(void)
{
    float fVar1;
    uint uVar4;
    u32 uVar5;

    u64 uVar6 = FUN_803826f0();
    int iVar3 = (int)((u64)uVar6 >> 0x20);
    uVar4 = session_get_state_flag();
    int iVar2 = ((int)(uVar4 ^ 2) >> 1) - ((uVar4 ^ 2) & uVar4);
    hud_element_init_from_layout(0x40000200, 0, (int)(uintptr_t)&arcsel_results_push_gate, 0);
    hud_set_item_padding_mode(FLOAT_8061a4f8, 1, 1, 1);
    fVar1 = FLOAT_8061a5a0;
    if (iVar2 < 0) {
        fVar1 = FLOAT_8061a5dc;
    }
    hud_add_spacer_item(fVar1, 0, (int)(uintptr_t)&arcsel_draw_results_detail);
    fVar1 = FLOAT_8061a5d4;
    if (iVar2 < 0) {
        fVar1 = FLOAT_8061a538;
    }
    hud_add_spacer_item(fVar1, 0, (int)(uintptr_t)&arcsel_draw_weapon_icons);
    uVar5 = session_get_state_flag();
    arcsel_layout_scoreboard_center(iVar3, uVar5, (uint)uVar6);
    *(float*)(iVar3 + 0x20c) = FLOAT_8061a4f8;
    FUN_8038273c();
}

// ============================================================================
// 0x80300438 / 0x8030045c / 0x80300480 / 0x803004a4
// arcsel_results_init_cb1..4 — identical hud trampolines into the variant init.
// ============================================================================
void arcsel_results_init_cb1(void) { arcsel_results_screen_init_variant(); }
void arcsel_results_init_cb2(void) { arcsel_results_screen_init_variant(); }
void arcsel_results_init_cb3(void) { arcsel_results_screen_init_variant(); }
void arcsel_results_init_cb4(void) { arcsel_results_screen_init_variant(); }
