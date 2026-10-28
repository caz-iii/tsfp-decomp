// Batch: batch_877 | Module: arcade map-editor screen lifecycle + save/load FSM
// Address range: 0x8030b27c - 0x8030b578
// Date: 2026-09-11
//
// The arcade map-editor's screen-lifecycle and save/load state machine. These sit
// above the per-mode input handlers (batch_876's aveditor_dispatch_input) and manage
// the editor screen's dirty flags (ctx+0x238), the level-changed latch, and the
// save/load progress FSM that eventually pops the screen. All state hangs off the
// active screen context DAT_80612be4 and the shared "last committed level id" global
// DAT_805d59a0.
//
//   aveditor_flush_edit_screen     0x8030b27c  thin wrapper -> FUN_80321a14 (commit the
//                                              in-progress edit screen).
//   aveditor_flush_if_dirty        0x8030b29c  flush when the screen is busy or the root
//                                              changed AND the edit-active flag is set.
//   aveditor_screen_input_gate     0x8030b314  per ctx+0x11854: flag 1 -> flush, else in
//                                              range [0,1) -> aveditor_dispatch_input on the
//                                              +0x10000 sub-context.
//   aveditor_level_changed         0x8030b364  true when the committed level id
//                                              (DAT_805d59a0) differs from ctx+0x204.
//   aveditor_latch_level_change    0x8030b384  when the level changed, latch the new id and
//                                              raise dirty bits 0x12.
//   aveditor_save_pending          0x8030b3c8  1 when dirty bit 2 is set and busy bit 0x800
//                                              is clear (a save is queued and can run).
//   aveditor_save_countdown_tick   0x8030b3f0  advance the post-save countdown (ctx+0x27c);
//                                              at 0x1d, clear it, drop dirty bit 2, pop screen.
//   aveditor_check_load_timeout    0x8030b468  when both this screen and its child are in
//                                              state 3 and the child timer exceeds the
//                                              threshold, set the retry bit (4) and clear 8.
//   aveditor_show_save_dialog      0x8030b4ec  raise dirty bit 8, format the save-failed
//                                              message (strings 0x9cf/0x9ce) and show a
//                                              simple dialog.
//   aveditor_save_load_tick        0x8030b578  the save/load FSM tick: on dirty bit 0x10
//                                              build+show the save dialog and clear it; then
//                                              per bits 8 (timeout), 4 (retry) or none
//                                              (loaded -> advance + countdown).
//
// NON-MATCHING: the PPC frame/varargs tails are carried through to feed FUN_80387fb8 /
// FUN_80341018 / FUN_8031e558 / FUN_80349068; the leftover-register returns the decompiler
// read from void helpers (FUN_8030b4ec, FUN_80341018) are dropped. aveditor_flush_if_dirty
// keeps the original short-circuit. Named helpers (hud_screen_pop, hud_create_simple_dialog,
// level_actor_is_loaded, get_string_by_id, hud_screen_find_root, aveditor_dispatch_input)
// resolve via the shared headers; every raw FUN_/DAT_/FLOAT_ symbol gets a per-file extern.

#include "common.h"
#include "hud.h"

extern "C" {

// raw FUN_ helpers
void FUN_80321a14(void);                         // commit the in-progress edit screen
int  FUN_8031d494(int arg);                      // resolve save-error detail code
u64  FUN_8031df80(void);                         // gather save context frame
u64  FUN_80341018(u64 a1, double a2, double a3, double a4, u64 a5, u64 a6, u64 a7, u64 a8);
void FUN_8031e558(u64 a1, double a2, double a3, double a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int countdown);                // finalize load + advance
void FUN_80349068(u64 a1, double a2, double a3, double a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int levelId);                  // retry save
void FUN_80387fb8(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int ctx, char* fmt, int v0, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10);

// active screen context + globals
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_806119ec;   // active player/screen index
extern int   DAT_80612bf4;   // dialog owner / root handle
extern unsigned char DAT_8051a820;   // per-screen state (screen root ptr slot)
extern int   DAT_805d59a0;   // last committed level id
extern float FLOAT_8061a614; // load-timeout threshold

} // extern "C"

// forward decls (intra-file references)
void aveditor_flush_edit_screen(void);
bool aveditor_level_changed(void);
void aveditor_save_countdown_tick(void);
void aveditor_check_load_timeout(void);
void aveditor_show_save_dialog(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                               u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                               int param_9, u32 param_10, u32 param_11, u32 param_12,
                               u32 param_13, u32 param_14, u32 param_15, u32 param_16);

// ---------------------------------------------------------------------------
// 0x8030b27c  aveditor_flush_edit_screen
// ---------------------------------------------------------------------------
void aveditor_flush_edit_screen(void)
{
    FUN_80321a14();
}

// ---------------------------------------------------------------------------
// 0x8030b29c  aveditor_flush_if_dirty
// ---------------------------------------------------------------------------
void aveditor_flush_if_dirty(void)
{
    int root;
    if ((((*(u32 *)(DAT_80612be4 + 0x25c) & 0x4044088) != 0) ||
         (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
          root != DAT_80612bf4)) &&
        (*(char *)(DAT_80612be4 + 0x11854) == '\x01')) {
        aveditor_flush_edit_screen();
    }
}

// ---------------------------------------------------------------------------
// 0x8030b314  aveditor_screen_input_gate
// ---------------------------------------------------------------------------
void aveditor_screen_input_gate(u64 param_1, double param_2, double param_3, double param_4,
                                double param_5, double param_6, u64 param_7, u64 param_8,
                                int param_9, u32 param_10, u32* param_11, int param_12,
                                int param_13, int param_14, u32 param_15, u32 param_16)
{
    char flag = *(char *)(DAT_80612be4 + 0x11854);
    if (flag == '\x01') {
        aveditor_flush_edit_screen();
    }
    else if ((flag < '\x01') && ('\0' <= flag)) {
        aveditor_dispatch_input(param_1, param_2, param_3, param_4, param_5, param_6,
                                param_7, param_8, param_9, (u32)(DAT_80612be4 + 0x10000),
                                param_11, param_12, param_13, param_14, param_15, param_16);
    }
}

// ---------------------------------------------------------------------------
// 0x8030b364  aveditor_level_changed
// ---------------------------------------------------------------------------
bool aveditor_level_changed(void)
{
    return DAT_805d59a0 != *(int *)(DAT_80612be4 + 0x204);
}

// ---------------------------------------------------------------------------
// 0x8030b384  aveditor_latch_level_change
// ---------------------------------------------------------------------------
void aveditor_latch_level_change(void)
{
    if (aveditor_level_changed()) {
        DAT_805d59a0 = *(int *)(DAT_80612be4 + 0x204);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x12;
    }
}

// ---------------------------------------------------------------------------
// 0x8030b3c8  aveditor_save_pending
// ---------------------------------------------------------------------------
u32 aveditor_save_pending(void)
{
    if ((*(u32 *)(DAT_80612be4 + 0x238) & 2) == 0) {
        return 0;
    }
    if ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x800) != 0) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x8030b3f0  aveditor_save_countdown_tick
// ---------------------------------------------------------------------------
void aveditor_save_countdown_tick(void)
{
    int root;
    *(int *)(DAT_80612be4 + 0x27c) = *(int *)(DAT_80612be4 + 0x27c) + 1;
    if (*(int *)(DAT_80612be4 + 0x27c) == 0x1d) {
        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        *(int *)(DAT_80612be4 + 0x27c) = 0;
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) & 0xfffffffd;
        hud_screen_pop(root);
    }
}

// ---------------------------------------------------------------------------
// 0x8030b468  aveditor_check_load_timeout
// ---------------------------------------------------------------------------
void aveditor_check_load_timeout(void)
{
    int root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (((*(int *)(root + 0x1ac) == 3) && (*(int *)(*(int *)(root + 0x2c) + 0x1ac) == 3)) &&
        (FLOAT_8061a614 < *(float *)(*(int *)(root + 0x2c) + 0x1b0))) {
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 4;
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) & 0xfffffff7;
    }
}

// ---------------------------------------------------------------------------
// 0x8030b4ec  aveditor_show_save_dialog
// ---------------------------------------------------------------------------
void aveditor_show_save_dialog(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                               u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                               int param_9, u32 param_10, u32 param_11, u32 param_12,
                               u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    int detail;
    char* fmt;
    int msg;
    int root;
    u64 frame = 0;

    (void)param_1;
    *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 8;
    detail = FUN_8031d494(param_9);
    fmt = (char *)get_string_by_id(0x9cf);
    FUN_80387fb8(frame, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 DAT_80612be4, fmt, detail, param_12, param_13, param_14, param_15, param_16);
    msg = get_string_by_id(0x9ce);
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    hud_create_simple_dialog(root, DAT_80612be4, msg);
}

// ---------------------------------------------------------------------------
// 0x8030b578  aveditor_save_load_tick
// ---------------------------------------------------------------------------
void aveditor_save_load_tick(u64 param_1, double param_2, double param_3, double param_4,
                             u64 param_5, u64 param_6, u64 param_7, u64 param_8,
                             u32 param_9, u32 param_10, u32 param_11, u32 param_12,
                             u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    int levelId  = DAT_805d59a0;
    int countdown = *(int *)(DAT_80612be4 + 0x27c);
    u32 loaded;
    u64 frame;

    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x10) != 0) {
        frame = FUN_8031df80();
        frame = FUN_80341018(frame, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        aveditor_show_save_dialog(frame, param_2, param_3, param_4, param_5, param_6, param_7,
                                  param_8, levelId, param_10, param_11, param_12, param_13,
                                  param_14, param_15, param_16);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) & 0xffffffef;
    }

    if ((*(u32 *)(DAT_80612be4 + 0x238) & 8) == 0) {
        if ((*(u32 *)(DAT_80612be4 + 0x238) & 4) == 0) {
            loaded = (u32)level_actor_is_loaded();
            if (loaded != 0) {
                FUN_8031e558(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                             param_8, countdown);
                aveditor_save_countdown_tick();
            }
        }
        else {
            FUN_80349068(param_1, param_2, param_3, param_4, param_5, param_6, param_7,
                         param_8, DAT_805d59a0);
            *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) & 0xfffffffb;
        }
    }
    else {
        aveditor_check_load_timeout();
    }
}
