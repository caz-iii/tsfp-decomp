// Batch: batch_875 | Module: arcade map-editor group / waypoint list editing
// Address range: 0x803098a0 - 0x8030a328
// Date: 2026-09-11
//
// The group / waypoint editing handlers for the arcade custom-map editor. A "group"
// is a fixed-stride record (0xe4 bytes, array based at ctx+0x288, active index in
// ctx+0x2f10 / insert cursor in ctx+0x2f12) holding up to 8 node entries (16 bytes
// each starting at group+0x64, count at group+0x58, kind at group+0x34). A second
// "type-10" list (base ctx+0xdd14, stride 0x8c, count ctx+0xdd10, active index via
// ctx+0x11858) holds up to 10 member/entry pairs (8 bytes each at +0x24, count +0x70,
// value +0x74, kind +0x80). These functions add / remove / reorder / rebind those
// entries against the global member list DAT_80612c14 and the current pick
// (ctx+0x2f18 / +0x2f28..+0x2f2c).
//
//   aveditor_handle_group_input       0x803098a0  the big group-node input handler:
//                                                 insert a node at the cursor (shifting
//                                                 the tail, capacity via FUN_8030f830),
//                                                 delete a node (shifting the tail down),
//                                                 step a transform handle (FUN_80313748),
//                                                 and move the cursor / enter focus views.
//   aveditor_enter_confirm_default    0x80309ee0  thin wrapper: screen_enter_confirm(..,0).
//   aveditor_check_roster_index       0x80309f04  bounds-check the pick's roster index
//                                                 against its entry count; roster_noop.
//   aveditor_remove_matching_grid_entry 0x80309fc8 scan the active group's entries for the
//                                                 one matching the current pick + grid
//                                                 coords and remove it (aselgrid_remove_entry_at).
//   aveditor_add_to_group             0x8030a090  append the current pick to the active
//                                                 type-10 group's entry list (max 10),
//                                                 else pop a "list full" back dialog (0xb65).
//   aveditor_remove_group_entry_if_type3 0x8030a1d0 if the active group kind == 3, remove
//                                                 the matching entry.
//   aveditor_count_selected_members   0x8030a220  count member nodes with the select bit
//                                                 (flags byte+0x14 bit 1) set.
//   aveditor_clear_group_entries      0x8030a248  zero the 10 entry slots of a group record.
//   aveditor_rebind_group_members     0x8030a274  for every type-10 group, rebind each
//                                                 entry that references the scratch member
//                                                 (FUN_80336bfc) back into the member.
//   aveditor_rebuild_group_from_selection 0x8030a328 clear the active group's entries then
//                                                 rebuild them from the currently selected
//                                                 members, and clear the select flags.
//
// NON-MATCHING: the __save_gpr_28 / __restore_gpr_28 prologue/epilogue intrinsics are
// modeled as raw externs; the value the decompiler reads back from __save_gpr_28 is the
// incoming register argument (an "any nodes present" / member-ptr flag) and is used only
// to gate the tail branches, so it is preserved as an int. FUN_803826f0 returns the
// scratch frame as a register pair (hi = member/context ptr) read as int. Two shared
// helpers are called with fewer args in the decompiler than their canonical prototype
// (screen_enter_confirm, aselgrid_remove_entry_at); the real register arguments are
// supplied here to match generated_decls.h. Named cross-module helpers resolve through
// the shared headers; every raw FUN_/DAT_/PTR_ symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// All named cross-module helpers (screen_enter_confirm, roster_noop, aselgrid_*,
// clear_member_select_flags, show_blocked_dialog, avanim_play_move_sound, hud_*,
// screen_enter_*, get_string_by_id, hud_screen_find_root) resolve through the shared
// headers. Only raw FUN_/DAT_/PTR_ symbols with no shared decl are declared here.
extern "C" {

// --- prologue/epilogue intrinsics (value = incoming saved register / flag) ---
int  __save_gpr_28(void);
void __restore_gpr_28(void);

// --- raw FUN_ helpers ---
u64  FUN_803826f0(void);                        // push scratch frame -> (hi=member ptr)
void FUN_8038273c(void);                        // pop scratch frame
int  FUN_8030f830(int need);                    // reserve capacity (0 = fail)
void FUN_80348238(int mode);                    // arm ctx dirty mode
int  FUN_80313748(int handle, int dir, int flag); // step transform handle
void FUN_803288f0(void);                        // notify group changed (kind 5)
void FUN_80329650(void);                        // group emptied hook (kind 5)
void FUN_8032c264(void);                        // commit / refresh group edit
void FUN_80328490(char groupIdx);               // begin group focus edit
int  FUN_8032a600(int group);                   // group has room / valid target?
int  FUN_8032a5ec(int group);                   // group entry needs sub-view?
int  FUN_80336690(int list, int index);         // -> ptr to entry[index] coord triple
void FUN_80336bfc(int memberRec, int value, int slot); // bind member into group slot
int  FUN_80336948(int kind);                    // pending-entry mask for kind
void FUN_8030fb78(int arg);                     // sync editor scratch from ctx
void FUN_80387fb8(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int ctx, char* fmt, int v0, u32 r6, u32 r7, u32 r8, u32 r9,
                  u32 r10);                      // sprintf-style into ctx format buffer

// --- active screen context + globals ---
extern int   DAT_80612be4;   // active screen context base
extern int   DAT_80612be8;   // editor scratch base
extern int   DAT_80612bf4;   // dialog owner handle
extern int   DAT_80612c14;   // head of the current member linked list
extern int   DAT_806119ec;   // active player/screen index
extern unsigned char DAT_8051a820;   // per-screen state table (screen root ptr slot)
extern void* PTR_FUN_80480eec;       // group entry focus view
extern void* PTR_FUN_80480efc;       // group entry sub focus view

} // extern "C"

// ---------------------------------------------------------------------------
// 0x803098a0  aveditor_handle_group_input
// ---------------------------------------------------------------------------
void aveditor_handle_group_input(void)
{
    int  savedFlag;
    int  root;
    u32  packed;
    u32  idx;
    int  group;
    int  count;
    int  off;
    short activeIdx;

    savedFlag = __save_gpr_28();
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc0) & 0x10) != 0) {
        if (((*(u32 *)(DAT_80612be4 + 0x25c) & 0x2000) == 0) &&
            (*(int *)(DAT_80612be4 + 0x2f20) < 0)) {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x2000;
            if (*(int *)(DAT_80612be4 + 0x2f18) != 0) {
                if (-1 < *(short *)(DAT_80612be4 + 0x2f10)) {
                    if ((*(u32 *)(DAT_80612be4 + 0x25c) & 4) != 0) {
                        if ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x410000) == 0) {
                            group = DAT_80612be4 + *(short *)(DAT_80612be4 + 0x2f10) * 0xe4 + 0x288;
                            if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                                FUN_80348238(0xd);
                                *(u32 *)(DAT_80612be4 + 0x238) =
                                    *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
                            }
                            idx = (u32)FUN_8030f830(4);
                            if (idx == 0) {
                                show_blocked_dialog(DAT_80612bf4);
                            }
                            else {
                                count = *(int *)(group + 0x58);
                                if (*(short *)(DAT_80612be4 + 0x2f12) < count) {
                                    off = count << 4;
                                    for (; *(short *)(DAT_80612be4 + 0x2f12) < count;
                                         count = count + -1) {
                                        int slot = group + off;
                                        off = off + -0x10;
                                        *(u32 *)(slot + 100)  = *(u32 *)(slot + 0x54);
                                        *(u32 *)(slot + 0x68) = *(u32 *)(slot + 0x58);
                                        *(u32 *)(slot + 0x6c) = *(u32 *)(slot + 0x5c);
                                        *(u32 *)(slot + 0x70) = *(u32 *)(slot + 0x60);
                                    }
                                }
                                {
                                    int base = group + *(short *)(DAT_80612be4 + 0x2f12) * 0x10;
                                    *(u32 *)(base + 100)  = *(u32 *)(DAT_80612be4 + 0x2f18);
                                    *(short *)(base + 0x68) = *(short *)(DAT_80612be4 + 0x2f28);
                                    *(short *)(base + 0x6a) = *(short *)(DAT_80612be4 + 0x2f2a);
                                    *(short *)(base + 0x6c) = *(short *)(DAT_80612be4 + 0x2f2c);
                                    *(u32 *)(base + 0x70) = 0;
                                }
                                *(int *)(group + 0x58) = *(int *)(group + 0x58) + 1;
                                if ((*(int *)(group + 0x34) == 5) &&
                                    ((*(u32 *)(DAT_80612be4 + 0x238) & 1) == 0)) {
                                    FUN_803288f0();
                                }
                                avanim_play_move_sound(0x157a);
                            }
                            *(short *)(DAT_80612be4 + 0x2f10) = (short)0xffff;
                            hud_input_reset_repeat_timers();
                        }
                    }
                }
            }
        }
    }

    if (savedFlag != 0) {
        if (*(short *)(DAT_80612be4 + 0x2f10) < 0) {
            if (*(int *)(DAT_80612be4 + 0x2f20) < 0) {
                *(short *)(DAT_80612be4 + 0x2f10) = (short)0xffff;
                screen_enter_dispatch();
                hud_input_reset_repeat_timers();
            }
        }
    }

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc0) & 0x40) != 0) {
        if (-1 < *(short *)(DAT_80612be4 + 0x2f10)) {
            group = DAT_80612be4 + *(short *)(DAT_80612be4 + 0x2f10) * 0xe4 + 0x288;
            if ((*(int *)(group + 0x34) == 5) && (*(int *)(group + 0x58) == 0)) {
                FUN_80329650();
            }
            FUN_8032c264();
            *(short *)(DAT_80612be4 + 0x2f10) = (short)0xffff;
            hud_input_reset_repeat_timers();
            __restore_gpr_28();
            return;
        }
    }

    if ((-1 < *(short *)(DAT_80612be4 + 0x2f10)) &&
        ((*(u32 *)(DAT_80612be4 + 0x25c) & 4) != 0)) {
        group = DAT_80612be4 + *(short *)(DAT_80612be4 + 0x2f10) * 0xe4 + 0x288;
        root = *(int *)(group + 0x34);
        if ((root == 0) || (root == 2) || (root == 4)) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x20) != 0) {
                *(int *)(group + 0x54) = FUN_80313748(*(int *)(group + 0x54), -1, 0);
                avanim_play_move_sound(0x1579);
            }
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x80) != 0) {
                *(int *)(group + 0x54) = FUN_80313748(*(int *)(group + 0x54), 1, 0);
                avanim_play_move_sound(0x1579);
            }
        }
    }

    packed = *(u32 *)(DAT_80612be4 + 0x2f20);
    if (-1 < (int)packed) {
        if (*(short *)(DAT_80612be4 + 0x2f10) < 0) {
            idx = packed & 0xffff;
            group = DAT_80612be4 + ((int)packed >> 0x10) * 0xe4 + 0x288;
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            bool doDelete = false;
            if ((*(u32 *)(root + 0xc0) & 0x40) == 0) {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if (((*(u32 *)(root + 0xc0) & 0x10) != 0) &&
                    ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x2000) == 0)) {
                    doDelete = true;
                }
            }
            else {
                doDelete = true;
            }
            if (doDelete) {
                FUN_8032c264();
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if (((*(u32 *)(root + 0xc0) & 0x10) != 0) &&
                    ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x2000) == 0)) {
                    avanim_play_move_sound(0x1578);
                    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                        FUN_80348238(0xf);
                        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
                    }
                }
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(u32 *)(root + 0xc0) & 0x40) != 0) {
                    avanim_play_move_sound(0x1577);
                    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
                        FUN_80348238(0xe);
                        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
                    }
                }
                if ((int)idx < *(int *)(group + 0x58) + -1) {
                    off = idx << 4;
                    for (; (int)idx < *(int *)(group + 0x58) + -1; idx = idx + 1) {
                        int slot = group + off;
                        off = off + 0x10;
                        *(u32 *)(slot + 100)  = *(u32 *)(slot + 0x74);
                        *(u32 *)(slot + 0x68) = *(u32 *)(slot + 0x78);
                        *(u32 *)(slot + 0x6c) = *(u32 *)(slot + 0x7c);
                        *(u32 *)(slot + 0x70) = *(u32 *)(slot + 0x80);
                    }
                }
                *(int *)(group + 0x58) = *(int *)(group + 0x58) + -1;
                if (*(int *)(group + 0x58) < 1) {
                    if (*(int *)(group + 0x34) == 5) {
                        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                        if ((*(u32 *)(root + 0xc0) & 0x40) != 0) {
                            FUN_80329650();
                        }
                    }
                }
            }

            activeIdx = (short)(packed >> 0x10);
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x10) != 0) {
                if ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x2000) == 0) {
                    *(short *)(DAT_80612be4 + 0x2f10) = activeIdx;
                    *(short *)(DAT_80612be4 + 0x2f12) = (short)packed;
                    hud_input_reset_repeat_timers();
                }
            }

            bool skipEnter = false;
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x80) != 0) {
                FUN_80328490((char)(packed >> 0x10));
                if (FUN_8032a600(group) != 0) {
                    if (FUN_8032a5ec(group) == 0) {
                        screen_enter_focus_view((int)(uintptr_t)&PTR_FUN_80480efc);
                        skipEnter = true;
                    }
                }
                if (!skipEnter) {
                    screen_enter_focus_view((int)(uintptr_t)&PTR_FUN_80480eec);
                }
            }

            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x20) != 0) {
                if (*(int *)(group + 0x58) < 8) {
                    if (FUN_8032a600(group) != 0) {
                        *(short *)(DAT_80612be4 + 0x2f10) = activeIdx;
                        *(short *)(DAT_80612be4 + 0x2f12) = (short)packed + 1;
                        hud_input_reset_repeat_timers();
                    }
                }
            }
        }
    }

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc0) & 0x10) != 0) {
        *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x2000;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x80309ee0  aveditor_enter_confirm_default
// ---------------------------------------------------------------------------
void aveditor_enter_confirm_default(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                    u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    (void)param_1;
    screen_enter_confirm(param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0);
}

// ---------------------------------------------------------------------------
// 0x80309f04  aveditor_check_roster_index
// ---------------------------------------------------------------------------
void aveditor_check_roster_index(void)
{
    int    frameHi;
    int    pick;

    frameHi = (int)(FUN_803826f0() >> 0x20);
    pick = *(int *)(DAT_80612be4 + 0x2f18);
    if ((pick != 0) && (*(int *)(DAT_80612be4 + 0x2f80) != 0)) {
        if (((int)*(short *)(frameHi + 4) <
             *(int *)(*(int *)(pick + *(int *)(pick + 0xc) * 4 + 0x2c) + 0x1c)) &&
            (-1 < *(short *)(frameHi + 4))) {
            roster_noop();
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80309fc8  aveditor_remove_matching_grid_entry
// Returns non-zero when a matching entry was found (and removed when the incoming
// commit flag was set). The caller (aveditor_add_to_group) uses this as the
// "already present" test before appending.
// ---------------------------------------------------------------------------
int aveditor_remove_matching_grid_entry(void)
{
    int    list;
    short* pCoord;
    int    index;
    int    off;
    int    commit;

    commit = __save_gpr_28();   // incoming commit flag (from prologue-saved register)
    index  = 0;
    off    = 0;
    list = *(int *)(DAT_80612be4 + 0x11858);
    list = DAT_80612be4 + *(short *)(list + 0xc) * 0x8c + 0xdd14;
    for (;;) {
        if (*(int *)(list + 0x20) <= index) {
            __restore_gpr_28();
            return 0;
        }
        pCoord = (short *)FUN_80336690(list, index);
        if ((*(int *)(DAT_80612be4 + 0x2f18) == *(int *)(list + off + 0x24)) &&
            (*(short *)(DAT_80612be4 + 0x2f28) == pCoord[0]) &&
            (*(short *)(DAT_80612be4 + 0x2f2a) == pCoord[1]) &&
            (*(short *)(DAT_80612be4 + 0x2f2c) == pCoord[2])) {
            if (commit != 0) {
                aselgrid_remove_entry_at(list, index);
            }
            __restore_gpr_28();
            return 1;
        }
        index = index + 1;
        off   = off + 8;
    }
}

// ---------------------------------------------------------------------------
// 0x8030a090  aveditor_add_to_group
// ---------------------------------------------------------------------------
void aveditor_add_to_group(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                           u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    u32     mask;
    int     alreadyPresent;
    short*  pCoord;
    char*   fmt;
    int     baseCount;
    int     group;
    u64     acc = 0;
    u32     in_r6 = 0, in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;

    (void)param_1;
    FUN_803826f0();
    group = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
    FUN_8030fb78(DAT_80612be8);
    baseCount = *(int *)(group + 0x20);
    mask = (u32)FUN_80336948(3);
    alreadyPresent = aveditor_remove_matching_grid_entry();
    if (alreadyPresent == 0) {
        mask = (u32)FUN_8030f830((~((int)(-(int)mask & ~(int)mask) >> 0x1f) & 8U) +
                                 (baseCount + 1) * 8);
        if (mask == 0) {
            show_blocked_dialog(DAT_80612bf4);
        }
        else {
            int count = *(int *)(group + 0x20);
            if (count < 10) {
                pCoord = (short *)FUN_80336690(group, count);
                *(u32 *)(group + count * 8 + 0x24) = *(u32 *)(DAT_80612be4 + 0x2f18);
                pCoord[0] = *(short *)(DAT_80612be4 + 0x2f28);
                pCoord[1] = *(short *)(DAT_80612be4 + 0x2f2a);
                pCoord[2] = *(short *)(DAT_80612be4 + 0x2f2c);
                *(int *)(group + 0x20) = *(int *)(group + 0x20) + 1;
            }
            else {
                fmt = (char *)get_string_by_id(0xb65);
                FUN_80387fb8(acc, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             DAT_80612be4, fmt, 10, in_r6, in_r7, in_r8, in_r9, in_r10);
                hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, 0);
                avanim_play_move_sound(0x1575);
            }
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8030a1d0  aveditor_remove_group_entry_if_type3
// ---------------------------------------------------------------------------
void aveditor_remove_group_entry_if_type3(void)
{
    int group = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
    if (*(int *)(group + 0x80) == 3) {
        aselgrid_remove_entry_at(group, 0);
    }
}

// ---------------------------------------------------------------------------
// 0x8030a220  aveditor_count_selected_members
// ---------------------------------------------------------------------------
int aveditor_count_selected_members(void)
{
    int count = 0;
    for (int node = DAT_80612c14; node != 0; node = *(int *)(node + 0x40)) {
        count = count + ((*(unsigned char *)(node + 0x14) >> 1) & 1);
    }
    return count;
}

// ---------------------------------------------------------------------------
// 0x8030a248  aveditor_clear_group_entries
// ---------------------------------------------------------------------------
void aveditor_clear_group_entries(int group)
{
    int off = 0;
    int n   = 10;
    do {
        *(u32 *)(group + off + 0x20) = 0;
        *(u32 *)(group + 0x70) = 0;
        off = off + 8;
        n   = n + -1;
    } while (n != 0);
}

// ---------------------------------------------------------------------------
// 0x8030a274  aveditor_rebind_group_members
// ---------------------------------------------------------------------------
void aveditor_rebind_group_members(void)
{
    int frameHi;
    int gi;
    int goff;

    frameHi = (int)(FUN_803826f0() >> 0x20);
    gi   = 0;
    goff = 0;
    for (;;) {
        if (*(int *)(DAT_80612be4 + 0xdd10) <= gi) {
            FUN_8038273c();
            return;
        }
        int group = DAT_80612be4 + goff + 0xdd14;
        if (*(int *)(group + 0x80) == 10) {
            int remaining = *(int *)(group + 0x70);
            int slot = 0;
            int eoff = 0;
            if (0 < remaining) {
                do {
                    if (*(int *)(group + eoff + 0x20) == frameHi) {
                        FUN_80336bfc(frameHi + 0x18, (int)*(short *)(group + 0x74), slot);
                        break;
                    }
                    slot      = slot + 1;
                    eoff      = eoff + 8;
                    remaining = remaining + -1;
                } while (remaining != 0);
            }
        }
        gi   = gi + 1;
        goff = goff + 0x8c;
    }
}

// ---------------------------------------------------------------------------
// 0x8030a328  aveditor_rebuild_group_from_selection
// ---------------------------------------------------------------------------
void aveditor_rebuild_group_from_selection(void)
{
    int group = DAT_80612be4 + *(short *)(*(int *)(DAT_80612be4 + 0x11858) + 0xc) * 0x8c + 0xdd14;
    aveditor_clear_group_entries(group);
    if (*(int *)(group + 0x80) == 10) {
        for (int node = DAT_80612c14; node != 0; node = *(int *)(node + 0x40)) {
            if ((*(unsigned char *)(node + 0x14) & 2) != 0) {
                FUN_80336bfc(node + 0x18, (int)*(short *)(group + 0x74), *(int *)(group + 0x70));
                *(int *)(group + *(int *)(group + 0x70) * 8 + 0x20) = node;
                *(int *)(group + 0x70) = *(int *)(group + 0x70) + 1;
            }
        }
    }
    clear_member_select_flags();
    aselgrid_clear_change_flags();
}
