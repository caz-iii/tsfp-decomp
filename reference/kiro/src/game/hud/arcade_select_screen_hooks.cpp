// Batch: batch_871 | Module: arcade-select screen enter/exit hooks + fade tick
// Address range: 0x80306d80 - 0x803071a4
// Date: 2026-09-11
//
// The per-sub-mode enter/exit wrappers around the arcade map-editor screen
// (arcade_edit_screen_enter / arcade_edit_screen_exit, batch_870), plus the
// layout accessors, cursor-field setter, camera-focus enter, dirty-check exit,
// and the screen fade tick. All hang off the active screen ctx DAT_80612be4;
// the sub-mode index at ctx+0x254 indexes the 0x18-byte screen-descriptor table
// at &DAT_8047f9b8 (fields: +0 id, +4 vtable_a @9bc, +8 vtable_b @9c0,
// +0xc vtable_c @9c4) and the parallel navigate-target pointer table
// &PTR_PTR_8047f988.
//
//   screen_enter_dispatch     0x80306d80  set flags 0x2080, enter, navigate to the
//                                         per-sub-mode target (PTR table).
//   screen_exit_clear_mode    0x80306dd4  clear flag mask 0x9fffff7f then exit.
//   screen_exit_clear_flag    0x80306e0c  clear only bit 0x80 then exit.
//   screen_enter_confirm      0x80306e3c  enter with sub-mode (ctx+0x1185c) dispatch:
//                                         modes 2/5 push a screen; mode 8 either pushes
//                                         or runs the confirm path (FUN_80338934 +
//                                         FUN_80338f54) and forces sub-mode 1; clears
//                                         the pending sub-mode afterward.
//   layout_is_not_0x47        0x80306f24  predicate: 0 iff id == 0x47.
//   layout_get_field8         0x80306f40  read +8 of the layout descriptor for *p.
//   cursor_set_fields         0x80306f68  copy 3 shorts into ctx+0x2f2e/+0x2f30/+0x264.
//   screen_enter_focus_view   0x80306f90  enter with camera focus: run the sub-mode
//                                         vtable hook (&DAT_8047f9bc), navigate, then
//                                         snapshot + ease the view to the cursor cell
//                                         (avanim_snapshot_current / avanim_begin_to).
//   screen_exit_dirty_check   0x803070a4  exit with unsaved-changes handling: query the
//                                         active element + sub-mode vtable_c, mark dirty
//                                         (FUN_80348238 / ctx+0x238 bit 0x20), run the
//                                         vtable_b hook, exit + avanim_begin_snap.
//   screen_fade_tick          0x803071a4  per-frame fade of FLOAT_80612bf0 toward a
//                                         target; direction depends on the screen stack
//                                         state (hud_screen_destroy_current) and ctx+0x238
//                                         bit 0x8000. Clamped to [FLOAT_8061a604,
//                                         FLOAT_8061a614].
//
// NON-MATCHING: the PPC int->float magic-bias idiom
// ((double)CONCAT44(0x43300000, x ^ 0x80000000) - DOUBLE_8061a5e0) is preserved
// verbatim. hud_screen_navigate_to / hud_screen_destroy_current are declared void
// in the shared header but return a live register at these call sites (their real
// bodies at 0x80047090 / 0x80046e48 produce a value); the return is captured via a
// typed function-pointer cast at the call site rather than by re-declaring the
// symbol (Compile-Clean rule 6). arcade_edit_screen_enter's decompiler-captured
// return is dropped (the shared decl is void). Named cross-module helpers resolve
// through the shared headers; every FUN_/DAT_/FLOAT_/DOUBLE_/PTR_ raw symbol gets a
// per-file extern below.

#include "common.h"
#include "hud.h"

// Raw per-file symbol externs use C linkage; the batch's own DEFINITIONS use C++
// linkage (below, outside this block) to match generated_decls.h.
extern "C" {

// --- raw FUN_ helpers referenced from this module ---
void FUN_80338934(int a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, u64 h); // confirm-dialog build
void FUN_80338f54(void);          // confirm-dialog commit
int  FUN_803826e8(void);          // active element handle
void FUN_80348238(int mode);      // mark ctx dirty (sound/anim)
void FUN_80382734(void);          // screen-event finalizer
void FUN_80345b88(void);          // fade/present step

// --- active screen context + globals ---
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_80612bf4;   // parent screen id (navigate target)
extern int   DAT_80612bec;   // current context-prompt id
extern int*  DAT_80612d68;   // present/flush flag pointer
extern float FLOAT_80612bf0; // screen fade level

// --- sub-mode descriptor table (0x18 stride) + navigate-target pointer table ---
extern int  DAT_8047f9b8;    // descriptor[.+0]  id / base
extern int  DAT_8047f9bc;    // descriptor[.+4]  vtable_a (enter focus hook)
extern int  DAT_8047f9c0;    // descriptor[.+8]  vtable_b (exit hook)
extern int  DAT_8047f9c4;    // descriptor[.+0xc] vtable_c (exit predicate)
extern void* PTR_PTR_8047f988; // per-sub-mode navigate target pointer table

// --- fade/view constants ---
extern int    DAT_806109c0;    // frame-rate denominator (int bits)
extern double DOUBLE_8061a5e0; // magic-bias subtrahend (int->float)
extern float  FLOAT_8061a5f0;  // focus zoom offset
extern float  FLOAT_8061a5f4;  // focus zoom base
extern float  FLOAT_8061a5f8;  // focus pos scale
extern float  FLOAT_8061a604;  // fade min
extern float  FLOAT_8061a614;  // fade max
extern float  FLOAT_8061a664;  // fade rate
extern float  FLOAT_80611a64;  // view zoom base
extern float  FLOAT_80611a68;  // view zoom scale
extern float  FLOAT_80611a6c;  // view zoom bias
extern float  FLOAT_80611a70;  // view zoom bias2

} // extern "C"

// ---------------------------------------------------------------------------
// 0x80306d80  screen_enter_dispatch
// ---------------------------------------------------------------------------
void screen_enter_dispatch(void)
{
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x2080;
    arcade_edit_screen_enter();
    hud_screen_navigate_to(DAT_80612bf4,
                           (int)(intptr_t)(&PTR_PTR_8047f988)[*(int *)(DAT_80612be4 + 0x254)],
                           -1);
}

// ---------------------------------------------------------------------------
// 0x80306dd4  screen_exit_clear_mode
// ---------------------------------------------------------------------------
void screen_exit_clear_mode(void)
{
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0x9fffff7f;
    arcade_edit_screen_exit();
}

// ---------------------------------------------------------------------------
// 0x80306e0c  screen_exit_clear_flag
// ---------------------------------------------------------------------------
void screen_exit_clear_flag(void)
{
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xffffff7f;
    arcade_edit_screen_exit();
}

// ---------------------------------------------------------------------------
// 0x80306e3c  screen_enter_confirm
// ---------------------------------------------------------------------------
void screen_enter_confirm(u64 param_2, u64 param_3, u64 param_4, u64 param_5,
                          u64 param_6, u64 param_7, u64 param_8, int param_9)
{
    int subMode;

    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x80;
    arcade_edit_screen_enter();
    subMode = *(int *)(DAT_80612be4 + 0x1185c);
    if (subMode != -1) {
        if (((subMode == 2) || (subMode == 5)) && (param_9 == 0)) {
            hud_screen_push();
        }
        else if (subMode == 8) {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xffffff7f;
            if (param_9 == 0) {
                hud_screen_push();
            }
            else {
                FUN_80338934(0, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
                FUN_80338f54();
            }
            *(int *)(DAT_80612be4 + 0x254) = 1;
        }
        else {
            hud_screen_push();
        }
        *(int *)(DAT_80612be4 + 0x1185c) = -1;
    }
}

// ---------------------------------------------------------------------------
// 0x80306f24  layout_is_not_0x47
// ---------------------------------------------------------------------------
u32 layout_is_not_0x47(int id)
{
    if (id != 0x47) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80306f40  layout_get_field8
// ---------------------------------------------------------------------------
u32 layout_get_field8(int* pId)
{
    unsigned char* pDesc;

    pDesc = (unsigned char *)arcsel_get_layout_desc(*pId);
    return *(u32 *)(pDesc + 8);
}

// ---------------------------------------------------------------------------
// 0x80306f68  cursor_set_fields
// ---------------------------------------------------------------------------
void cursor_set_fields(unsigned short* pSrc)
{
    *(unsigned short *)(DAT_80612be4 + 0x2f2e) = pSrc[0];
    *(unsigned short *)(DAT_80612be4 + 0x2f30) = pSrc[1];
    *(int *)(DAT_80612be4 + 0x264) = (int)(short)pSrc[2];
}

// ---------------------------------------------------------------------------
// 0x80306f90  screen_enter_focus_view
// ---------------------------------------------------------------------------
void screen_enter_focus_view(int param_1)
{
    int    navResult;
    double zoom;
    void (**vtableA)(void);

    vtableA = (void (**)(void))(&DAT_8047f9bc + *(int *)(DAT_80612be4 + 0x254) * 0x18);
    if (*vtableA != (void (*)(void))0x0) {
        (*vtableA)();
    }
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x4000000;
    arcade_edit_screen_enter();
    navResult = ((int (*)(int, int, int))(void *)hud_screen_navigate_to)(DAT_80612bf4, param_1, -1);
    DAT_80612bec = 0x10;
    zoom = (double)(FLOAT_8061a5f8 * (FLOAT_8061a5f0 + *(float *)(navResult + 0x4c)));
    avanim_snapshot_current();
    avanim_begin_to(
        (double)(FLOAT_8061a5f8 +
                 (float)((double)CONCAT44(0x43300000,
                                          (int)*(short *)(DAT_80612be4 + 0x2f2e) ^ 0x80000000) -
                         DOUBLE_8061a5e0)),
        (double)(FLOAT_8061a5f8 +
                 (float)((double)CONCAT44(0x43300000,
                                          (int)*(short *)(DAT_80612be4 + 0x2f30) ^ 0x80000000) -
                         DOUBLE_8061a5e0)),
        (double)(float)(zoom * (double)FLOAT_80611a68 + (double)FLOAT_80611a70),
        (double)(FLOAT_8061a5f4 * FLOAT_80611a64 + FLOAT_80611a6c),
        *(u32 *)(DAT_80612be4 + 0x264));
}

// ---------------------------------------------------------------------------
// 0x803070a4  screen_exit_dirty_check
// ---------------------------------------------------------------------------
void screen_exit_dirty_check(void)
{
    bool  vtcResult;
    int   element;
    int   descOff;
    int   negId;
    int   ret;
    int (**vtableC)(void);
    void (**vtableB)(void);

    element = FUN_803826e8();
    vtcResult = false;
    descOff = *(int *)(DAT_80612be4 + 0x254) * 0x18;
    negId = -*(int *)(&DAT_8047f9b8 + descOff);
    vtableC = (int (**)(void))(&DAT_8047f9c4 + descOff);
    if ((*vtableC != (int (*)(void))0x0) && (ret = (*vtableC)(), ret != 0)) {
        vtcResult = true;
    }
    *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xfbffffff;
    if (negId + -1 != (int)((u32)(negId == 1) + negId + -2)) {
        ret = hud_element_has_unsaved_changes(element);
        if (((ret != 0) || (vtcResult)) && ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0)) {
            FUN_80348238(*(int *)(&DAT_8047f9b8 + *(int *)(DAT_80612be4 + 0x254) * 0x18));
            *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
        }
    }
    vtableB = (void (**)(void))(&DAT_8047f9c0 + descOff);
    if (*vtableB != (void (*)(void))0x0) {
        (*vtableB)();
    }
    arcade_edit_screen_exit();
    avanim_begin_snap();
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x803071a4  screen_fade_tick
// ---------------------------------------------------------------------------
void screen_fade_tick(void)
{
    int stackTop;

    *DAT_80612d68 = 0;
    hud_element_update();
    FUN_80345b88();
    stackTop = ((int (*)(void))(void *)hud_screen_destroy_current)();
    if ((((stackTop != 0) &&
          (stackTop = ((int (*)(void))(void *)hud_screen_destroy_current)(),
           (*(u32 *)(stackTop + 0x214) & 1) == 0)) &&
         (stackTop = ((int (*)(void))(void *)hud_screen_destroy_current)(),
          stackTop != DAT_80612bf4)) &&
        ((*(u32 *)(DAT_80612be4 + 0x238) & 0x8000) == 0)) {
        FLOAT_80612bf0 =
            FLOAT_8061a664 *
                (FLOAT_8061a614 /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a5e0)) +
            FLOAT_80612bf0;
        if (FLOAT_80612bf0 <= FLOAT_8061a614) {
            return;
        }
        FLOAT_80612bf0 = FLOAT_8061a614;
        return;
    }
    FLOAT_80612bf0 =
        -(FLOAT_8061a664 *
              (FLOAT_8061a614 /
               (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061a5e0)) -
          FLOAT_80612bf0);
    if (FLOAT_80612bf0 < FLOAT_8061a604) {
        FLOAT_80612bf0 = FLOAT_8061a604;
    }
}
