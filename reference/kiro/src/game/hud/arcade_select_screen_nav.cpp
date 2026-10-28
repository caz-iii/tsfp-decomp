// Batch: batch_861 | Module: arcade-select scoreboard/results navigation + lifecycle
// Address range: 0x8030050c - 0x80300968
//
// Navigation, lifecycle and layout-accessor helpers for the arcade-select
// scoreboard/results screens.
//
// arcsel_nav_advance_if_flagged advances to the next entry and pushes the next
// screen when the element's status word (obj->0x168->0xc0) has bit 0x80000 set.
// arcsel_results_multiscreen_init builds the multi-column results screen: it inits
// the element (confirm gate), creates one child element per active player from the
// PTR_arcsel_results_init_cb1_804777f0 table (0x10 stride), shows it, registers the
// button handler and clears DAT_80612bc8. arcsel_scoreboard_push_next and
// arcsel_scoreboard_reset_and_push are the scoreboard advance buttons; the latter
// clears each per-slot marquee offset, resets the arrow flags and selection index
// (DAT_80612bac) before pushing.
//
// arcsel_scoreboard_advance_players is the per-player screen dismiss/advance state
// machine: for each visible slot it either pops the screen, pushes the next
// scoreboard sub-screen, or flags the current screen dirty and refreshes.
// arcsel_draw_title_label draws the screen title/prompt string (id 0x131) right of
// the menu element. arcsel_simple_screen_init is a bare centred-element init.
// arcsel_clear_status_bit clears status bit 0x400. arcsel_get_layout_desc and
// arcsel_get_results_column_cfg return layout/column descriptor pointers by index.
//
// Named cross-module helpers (hud_*, arcsel_*, text_*, get_string_by_id,
// session_get_state_flag) resolve through the shared generated headers; only raw
// FUN_/DAT_/FLOAT_/DOUBLE_/PTR_ symbols and the __save/restore intrinsics get
// per-file externs.

#include "common.h"
#include "hud.h"
#include "text.h"

// ---- raw symbols referenced by this translation unit -------------------------
extern "C" {
u64  __save_gpr_28(void);
void __restore_gpr_28(void);

u64  FUN_803826e8(void);            // hud active-screen ctx (pair)
u64  FUN_803826f0(void);            // hud active-screen ctx (pair)
void FUN_8038273c(void);            // hud screen finalize
void FUN_80382734(void);            // hud event-list finalize
int* FUN_80339b90(int, int);        // level/record lookup by id

extern float  FLOAT_8061a418;
extern float  FLOAT_8061a444;
extern float  FLOAT_8061a4f8;
extern float  FLOAT_8061a510;
extern double DOUBLE_8061a430;

extern int    DAT_80611748;   // active menu element ptr
extern int    DAT_806119e0;   // element center anchor
extern int    DAT_806119ec;   // current player savegame block index
extern int**  DAT_805cf280;   // per-player screen-state block table
extern int    DAT_805d0914;   // per-slot marquee-scroll offset table
extern int    DAT_805d0924;   // per-slot dismiss-state table
extern int    DAT_80612bac;   // selected column index
extern int    DAT_80612bc8;   // multiscreen "built" latch
extern int    DAT_80612e74;   // suppress-advance flag
extern int    DAT_80477850;   // layout descriptor table base (0x18 stride)
extern short  DAT_80477bca;   // layout-index table (0x62 stride)
extern int    DAT_80477bd4;   // results-column config table base (0x31 stride)
extern int    DAT_80477c80;   // results-column flags table base (0x31 stride)
extern int    PTR_arcsel_results_init_cb1_804777f0; // results init-cb ptr table (0x10 stride)
}

// ============================================================================
// 0x80300924  arcsel_clear_status_bit
// Clear status bit 0x400 on the current player's screen-state block.
// ============================================================================
void arcsel_clear_status_bit(void)
{
    *DAT_805cf280[0] = *DAT_805cf280[0] & 0xfffffbff;
}

// ============================================================================
// 0x80300940  arcsel_get_layout_desc
// Return the layout descriptor for a screen index: DAT_80477850 + (indexed
// short in DAT_80477bca) * 0x18.
// ============================================================================
void* arcsel_get_layout_desc(int index)
{
    return (void*)((int)&DAT_80477850 + (short)(&DAT_80477bca)[index * 0x62] * 0x18);
}

// ============================================================================
// 0x80300968  arcsel_get_results_column_cfg
// Return the results-column config pointer for a column: the base entry, or (when
// flag bit 0x4 is set) an entry selected via a record lookup (FUN_80339b90).
// ============================================================================
u32* arcsel_get_results_column_cfg(int column, int* pRecordKey)
{
    u32* puVar2;
    if (((&DAT_80477c80)[column * 0x31] & 4) == 0) {
        puVar2 = (u32*)&DAT_80477bd4 + column * 0x31;
    } else {
        int* piVar1 = FUN_80339b90((int)pRecordKey, 0xc77668);
        puVar2 = (u32*)&DAT_80477bd4 + (uint)*(ushort*)((int)piVar1 + 10) * 7 + column * 0x31;
    }
    return puVar2;
}

// ============================================================================
// 0x80300634  arcsel_scoreboard_push_next
// Scoreboard advance button: both session-flag branches push the next screen.
// ============================================================================
void arcsel_scoreboard_push_next(void)
{
    int iVar1 = session_get_state_flag();
    if (iVar1 == 1) {
        hud_screen_push();
    } else {
        hud_screen_push();
    }
}

// ============================================================================
// 0x8030050c  arcsel_nav_advance_if_flagged
// Advance to the next entry and push the next screen when the element's status
// word (obj->0x168 -> 0xc0) has bit 0x80000 set.
// ============================================================================
void arcsel_nav_advance_if_flagged(int obj)
{
    if ((*(uint*)(*(int*)(obj + 0x168) + 0xc0) & 0x80000) != 0) {
        arcsel_nav_next_entry();
        hud_screen_push();
    }
}

// ============================================================================
// 0x80300714  arcsel_draw_title_label
// Draw the screen title/prompt string (id 0x131) shadowed, positioned to the
// right edge of the active menu element (DAT_80611748).
// ============================================================================
void arcsel_draw_title_label(void)
{
    int iVar1 = session_get_state_flag();
    int iVar3 = 0xb;
    if (2 < iVar1) {
        iVar3 = 9;
    }
    byte* pbVar2 = (byte*)get_string_by_id(0x131);
    text_set_font_table(iVar3);
    double dVar4 = text_get_char_width(pbVar2);
    text_draw_char_shadowed(
        -(double)(float)(dVar4 * (double)FLOAT_8061a444 -
            (double)(float)((double)CONCAT44(0x43300000,
                *(uint*)(DAT_80611748 + 8) ^ 0x80000000) - DOUBLE_8061a430)),
        (double)(float)((double)CONCAT44(0x43300000,
            *(int*)(DAT_80611748 + 0x1c) - 0x14U ^ 0x80000000) - DOUBLE_8061a430),
        pbVar2, 0x40787880, 0x5080);
}

// ============================================================================
// 0x803008b4  arcsel_simple_screen_init
// Bare screen init: install auto-advance callbacks, init a default-layout element,
// set its base scale (obj+0x64) and centre it.
// ============================================================================
void arcsel_simple_screen_init(int obj)
{
    *(void**)(obj + 0x1c0) = (void*)&arcsel_auto_advance_if_ready;
    *(void**)(obj + 0x1c4) = (void*)&arcsel_auto_advance_next;
    hud_element_init_from_layout(0, 0, 0, 0);
    *(float*)(obj + 0x64) = FLOAT_8061a510;
    hud_element_apply_center_xy(&DAT_806119e0);
    *(float*)(obj + 0x20c) = FLOAT_8061a4f8;
}

// ============================================================================
// 0x80300550  arcsel_results_multiscreen_init
// Build the multi-column results screen: init the element (confirm gate), create
// one child per active player from the PTR_arcsel_results_init_cb1_804777f0 table
// (0x10 stride), show it, register the button handler and clear the built latch.
// ============================================================================
void arcsel_results_multiscreen_init(void)
{
    int iVar3, iVar4, iVar5;

    int iVar1 = (int)__save_gpr_28();
    uint uVar2 = session_get_state_flag();
    hud_element_init_from_layout(
        (int)(-(uint)(2 < uVar2) - ((int)uVar2 >> 0x1f) & 0x40000000 | 0x200000), 0,
        (int)(uintptr_t)&arcsel_results_push_gate, 0);
    iVar5 = 0;
    for (iVar4 = 0; iVar3 = session_get_state_flag(), iVar4 < iVar3; iVar4 = iVar4 + 1) {
        hud_element_create((u32)iVar1, (u32)((int)&PTR_arcsel_results_init_cb1_804777f0 + iVar5), 2);
        iVar5 = iVar5 + 0x10;
    }
    hud_screen_show_by_id(iVar1, -0x7fb88810);
    *(float*)(iVar1 + 0x20c) = FLOAT_8061a4f8;
    iVar1 = get_string_by_id(0xb02);
    hud_screen_register_handler_global(0, iVar1, 0, 0);
    hud_element_finalize_frame();
    DAT_80612bc8 = 0;
    __restore_gpr_28();
}

// ============================================================================
// 0x80300690  arcsel_scoreboard_reset_and_push
// Reset the scoreboard scroll/selection state (clear each per-slot marquee offset,
// reset the arrow flags and clear the selected column) then push the next screen.
// ============================================================================
void arcsel_scoreboard_reset_and_push(void)
{
    int iVar1, iVar2, iVar3;

    FUN_803826f0();
    iVar3 = 0;
    for (iVar2 = 0; iVar1 = session_get_state_flag(), iVar2 < iVar1; iVar2 = iVar2 + 1) {
        arcsel_clear_flag((u32*)(&DAT_805d0914 + iVar3));
        iVar3 = iVar3 + 4;
    }
    arcsel_prompt_set_arrow_flags((int)DAT_805cf280 + 0x25c, 1, 0);
    DAT_80612bac = 0xffffffff;
    arcsel_scoreboard_push_next();
    FUN_8038273c();
}

// ============================================================================
// 0x803007c8  arcsel_scoreboard_advance_players
// Per-player screen dismiss/advance state machine: for each visible slot, pop the
// screen (already-dismissed), push the next sub-screen (confirm bit set), or flag
// the current screen dirty and refresh (arcsel_mark_all_dirty / _screen_update).
// ============================================================================
void arcsel_scoreboard_advance_players(void)
{
    int iVar1, iVar2, iVar4;

    FUN_803826e8();
    uint* puVar3 = (uint*)DAT_805cf280[DAT_806119ec];
    if (DAT_80612e74 == 0) {
        if (((*DAT_805cf280[0] & 0x400) == 0) && ((*DAT_805cf280[0] & 4) == 0)) {
            iVar4 = 0;
            for (iVar2 = 0; iVar1 = session_get_state_flag(), iVar2 < iVar1; iVar2 = iVar2 + 1) {
                if (*(int*)((int)&DAT_805d0924 + iVar4) == 0) {
                    if ((*puVar3 & 4) == 0) {
                        puVar3[3] = (uint)FLOAT_8061a418;
                        *puVar3 = *puVar3 | 8;
                        arcsel_mark_all_dirty();
                        arcsel_screen_update();
                    } else {
                        arcsel_scoreboard_push_next();
                    }
                } else {
                    hud_screen_pop_current();
                }
                iVar4 = iVar4 + 4;
            }
        } else {
            hud_screen_pop_current();
        }
    }
    FUN_80382734();
}
