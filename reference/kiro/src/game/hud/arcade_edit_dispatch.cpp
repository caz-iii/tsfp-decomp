// Batch: batch_876 | Module: arcade map-editor top-level input dispatch + zone/group input
// Address range: 0x8030a3dc - 0x8030b23c
// Date: 2026-09-11
//
// The top-level arcade map-editor input dispatcher and its supporting helpers. The
// dispatcher (aveditor_dispatch_input) routes each frame's input to the mode handler
// selected by ctx+0x254 (0 = free edit, 1 = roster, 2 = create-cell, 3.. = group,
// 4 = zone), calling into the batch_874/875 aveditor_* handlers. This file adds the
// zone-input handler, the "can add / has pick / can exit" predicates, the cursor->grid
// helpers, and the group member-removal helpers, all against the active screen context
// DAT_80612be4.
//
//   aveditor_group_remove_member       0x8030a3dc  remove every entry in a group that
//                                                  references the scratch member, shift-
//                                                  compacting the list (leaf_31e8) and
//                                                  decrementing the count, then FUN_803364d0.
//   aveditor_remove_member_all_groups  0x8030a478  run the above over every type-10 group.
//   aveditor_store_cursor_grid_pos     0x8030a4f0  project the cursor X/Y through the grid
//                                                  scale/offset and store into ctx+0x2f94/98.
//   aveditor_autoplace_layout_entries  0x8030a58c  collect the valid layout entries and
//                                                  auto-place up to 10 of them (FUN_8032cd1c).
//   aveditor_has_active_pick           0x8030a650  per edit-mode (ctx+0x1185c), report
//                                                  whether there is a valid current pick.
//   aveditor_cursor_hits_zone          0x8030a730  hit-test the cursor against the zone /
//                                                  door maps; 1 = hit.
//   aveditor_handle_zone_input         0x8030a7b0  the mode-8 (zone/group) input handler:
//                                                  add/remove group members, place records,
//                                                  step transforms, toggle member select,
//                                                  and the confirm/enter transitions.
//   aveditor_can_exit                  0x8030b084  1 when the editor has no pending edit
//                                                  state and may leave the screen.
//   aveditor_dispatch_input            0x8030b174  top-level per-frame input router by
//                                                  ctx+0x254 mode -> the aveditor_* handlers.
//   aveditor_check_add_allowed         0x8030b23c  gate: when ctx+0x11854 flag is set,
//                                                  delegate to FUN_8032196c, else allow.
//
// NON-MATCHING: several void helpers (aveditor_place_record / _rebuild_group_from_selection /
// _check_roster_index / _create_cell_at_cursor / _add_to_group, hud_screen_destroy_current)
// have their leftover-register "return" read by the decompiler; the real code either returns
// void or is a getter. Where the value is only forwarded into a screen_enter_confirm frame it
// is replaced with 0/param; where it is a genuine getter (hud_screen_destroy_current) it is
// recovered via a function-pointer cast to preserve the branch. screen_enter_confirm takes 8
// args in the shared decl (the decompiler shows 9); the first PPC frame arg is dropped.
// roster_try_place_character is called with the canonical 16-arg signature. countLeadingZeros
// >> 5 is the standard is-zero->1 idiom. Named helpers resolve via generated_decls.h; every
// raw FUN_/__gpr symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// Named cross-module + prior-batch aveditor_* helpers resolve through the shared
// headers (generated_decls.h via hud.h). Only raw FUN_/__gpr symbols are declared here.
extern "C" {

// prologue/epilogue intrinsics (value = incoming saved register / flag)
int  __save_gpr_28(void);
void __restore_gpr_28(void);

// raw FUN_ helpers
u64  FUN_803826f0(void);                          // push scratch frame -> (hi=ptr)
void FUN_8038273c(void);                          // pop scratch frame
void FUN_803826e8(void);                          // push scratch frame (no payload)
void FUN_80382734(void);                          // pop scratch frame
void FUN_803364d0(int group);                     // rebuild group after removal
int  FUN_8032cf70(int screenSel);                 // resolve layout descriptor
int  FUN_8032cebc(int desc, int index);           // is layout entry[index] valid?
int  FUN_8032cd1c(void);                           // auto-place one entry -> group count
int  FUN_80345de8(double x, double y);             // door/zone hit-test A (-1 = miss)
int  FUN_80345e80(double x, double y);             // door/zone hit-test B (0 = miss)
int  FUN_80332c04(void);                           // mode predicate (roster busy?)
u32  FUN_80332e00(int arg);                        // resolve layout->confirm value
u64  FUN_80332cd8(short a, int b);                 // build confirm frame value
int  FUN_80332a84(int layoutId);                   // layout accepts type-2 placement?
u32  FUN_80332b90(void);                           // type-8 placement allowed?
int  FUN_803214c0(void);                           // pending-change predicate
u32  FUN_8032196c(int arg);                        // add-allowed sub-check
u32  FUN_80342698(int member, short* rec);         // fetch placed record
void FUN_80342298(short* out);                     // read cursor grid coords
int  FUN_80313748(int handle, int dir, int flag);  // step transform handle
void FUN_80387fb8(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7, u64 a8,
                  int ctx, char* fmt, int v0, u32 r6, u32 r7, u32 r8, u32 r9,
                  u32 r10);                          // sprintf-style into ctx format buffer

// active screen context + globals
extern int DAT_80612be4;    // active screen context base
extern int DAT_80612bf4;    // dialog owner handle
extern int DAT_806119ec;    // active player/screen index
extern unsigned char DAT_8051a820;   // per-screen state (screen root ptr slot)
extern unsigned char DAT_8051a834;   // per-screen cursor X slot
extern unsigned char DAT_8051a838;   // per-screen cursor Y slot
extern unsigned char DAT_80477c80;   // per-layout attribute flag table
extern int DAT_80612c84;    // cursor->grid X bias
extern int DAT_80612c88;    // cursor->grid Y bias

extern float FLOAT_80612c54;         // grid X scale
extern float FLOAT_80612c58;         // grid Y scale
extern float FLOAT_80612c74;         // grid X offset
extern float FLOAT_80612c78;         // grid Y offset
extern double DOUBLE_8061a5e0;       // int->float bias constant

} // extern "C"

// forward decls (intra-file references / address identity)
void aveditor_group_remove_member(void);
void aveditor_store_cursor_grid_pos(void);
int  aveditor_autoplace_layout_entries(void);
u32  aveditor_has_active_pick(void);
int  aveditor_cursor_hits_zone(void);
void aveditor_handle_zone_input(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8);

// ---------------------------------------------------------------------------
// 0x8030a3dc  aveditor_group_remove_member
// ---------------------------------------------------------------------------
void aveditor_group_remove_member(void)
{
    int  group;
    int* pEntry;
    int  tail;
    int  i;
    int  member;

    member = __save_gpr_28();     // scratch member ptr (incoming register)
    group  = member;              // decompiler read group base + member from the same frame
    // The group base is the scratch-frame hi word; the member key is its lo word.
    // Both are carried in via the prologue-saved register set.
    for (i = 0; i < *(int *)(group + 0x70); i = i + 1) {
        pEntry = (int *)(group + 0x20 + i * 8);
        if (*pEntry == member) {
            tail = 10 - (i + 1);
            if (0 < tail) {
                leaf_31e8((u32)(uintptr_t)pEntry, group + 0x20 + (i + 1) * 8, tail * 8);
                i = i + -1;
                *(int *)(group + 0x70) = *(int *)(group + 0x70) + -1;
            }
        }
    }
    FUN_803364d0(group);
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// 0x8030a478  aveditor_remove_member_all_groups
// ---------------------------------------------------------------------------
void aveditor_remove_member_all_groups(void)
{
    int i;
    int off;

    FUN_803826f0();
    off = 0;
    for (i = 0; i < *(int *)(DAT_80612be4 + 0xdd10); i = i + 1) {
        if (*(int *)(DAT_80612be4 + off + 0xdd94) == 10) {
            aveditor_group_remove_member();
        }
        off = off + 0x8c;
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x8030a4f0  aveditor_store_cursor_grid_pos
// ---------------------------------------------------------------------------
void aveditor_store_cursor_grid_pos(void)
{
    double bias = DOUBLE_8061a5e0;
    *(float *)(DAT_80612be4 + 0x2f94) =
        FLOAT_80612c58 *
        (*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0) -
         (float)((double)CONCAT44(0x43300000, DAT_80612c88 ^ 0x80000000) - DOUBLE_8061a5e0)) +
        FLOAT_80612c78;
    *(float *)(DAT_80612be4 + 0x2f98) =
        FLOAT_80612c54 *
        (*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0) -
         (float)((double)CONCAT44(0x43300000, DAT_80612c84 ^ 0x80000000) - bias)) +
        FLOAT_80612c74;
}

// ---------------------------------------------------------------------------
// 0x8030a58c  aveditor_autoplace_layout_entries
// ---------------------------------------------------------------------------
int aveditor_autoplace_layout_entries(void)
{
    int desc;
    int valid;
    int off;
    int i;
    int placed;
    int list[14];

    FUN_803826e8();
    desc = FUN_8032cf70(*(int *)(DAT_80612be4 + 0x11858));
    valid = 0;
    off = 0;
    for (i = 0; i < *(int *)(DAT_80612be4 + 0x41e0); i = i + 1) {
        if (FUN_8032cebc(desc, i) != 0) {
            *(int *)((int)list + off) = i;
            valid = valid + 1;
            off = off + 4;
        }
    }
    placed = 0;
    while ((placed < valid) && (FUN_8032cd1c() < 10)) {
        placed = placed + 1;
    }
    FUN_80382734();
    return placed;
}

// ---------------------------------------------------------------------------
// 0x8030a650  aveditor_has_active_pick
// ---------------------------------------------------------------------------
u32 aveditor_has_active_pick(void)
{
    int   mode;
    u32   isZero;
    int   found;
    short coords[8];

    mode = *(int *)(DAT_80612be4 + 0x1185c);
    if (mode == 2) {
        if (*(int *)(DAT_80612be4 + 0x2f1c) != 0) {
            return 1;
        }
    }
    else if (mode < 2) {
        if (((mode == 0) && (*(int *)(DAT_80612be4 + 0x2f18) != 0)) &&
            (-1 < *(int *)(DAT_80612be4 + 0x2f34))) {
            return 1;
        }
    }
    else if (mode == 7) {
        FUN_80342298(coords);
        aveditor_check_roster_index();
        // aveditor_check_roster_index gates the removal; when a matching entry
        // exists the grid-remove reports found != 0.
        found = aveditor_remove_matching_grid_entry();
        isZero = countLeadingZeros((u32)found);
        return isZero >> 5;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8030a730  aveditor_cursor_hits_zone
// ---------------------------------------------------------------------------
int aveditor_cursor_hits_zone(void)
{
    int hit;
    hit = FUN_80345de8((double)*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0),
                       (double)*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0));
    if ((hit == -1) &&
        (hit = FUN_80345e80((double)*(float *)(&DAT_8051a834 + DAT_806119ec * 0x1e0),
                            (double)*(float *)(&DAT_8051a838 + DAT_806119ec * 0x1e0)), hit == 0)) {
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 0x8030a7b0  aveditor_handle_zone_input
// ---------------------------------------------------------------------------
void aveditor_handle_zone_input(u64 param_1, u64 param_2, u64 param_3, u64 param_4,
                                u64 param_5, u64 param_6, u64 param_7, u64 param_8)
{
    int    root;
    u32    ok;
    char*  fmt;
    int    slot;
    bool   accept;
    u32    cval;
    unsigned short cshort;
    u32    in_r6 = 0, in_r7 = 0, in_r8 = 0, in_r9 = 0, in_r10 = 0;
    u64    frame = 0;
    u32    local_38, local_30, local_28, local_20, local_10;
    unsigned short local_34, local_2c, local_24, local_1c, local_c;
    short  asStack_18[4];

    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if (((((*(u32 *)(root + 0xc0) & 0x10) != 0) && (*(int *)(DAT_80612be4 + 0x248) == 1)) &&
         (ok = aveditor_has_active_pick(), ok != 0)) &&
        ((root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
          (*(u32 *)(root + 0xc0) & 0x10) != 0 &&
          (root = aveditor_autoplace_layout_entries(), root != 0)))) {
        fmt = (char *)get_string_by_id(0xb54);
        FUN_80387fb8(frame, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     DAT_80612be4, fmt, 10, in_r6, in_r7, in_r8, in_r9, in_r10);
        hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, 0);
        avanim_play_move_sound(0x1575);
        return;
    }

    if (*(int *)(DAT_80612be4 + 0x1185c) == 7) {
        FUN_80342298((short *)&local_10);
        local_28 = local_10;
        local_24 = local_c;
        aveditor_check_roster_index();
        // real predicate: does the pick have a valid roster index?
        if (aveditor_remove_matching_grid_entry() == 0) {
            *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xfffffffb;
        }
        fmt = (char *)get_string_by_id(0xb65);
        FUN_80387fb8(frame, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                     DAT_80612be4, fmt, 10, in_r6, in_r7, in_r8, in_r9, in_r10);
        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        if ((*(u32 *)(root + 0xc0) & 0x20) == 0) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if (((*(u32 *)(root + 0xc0) & 0x10) == 0) ||
                ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x2000) != 0)) {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if ((*(u32 *)(root + 0xc0) & 0x40) != 0) {
                    aveditor_remove_group_entry_if_type3();
                    hud_input_reset_repeat_timers();
                    return;
                }
            }
            else {
                FUN_80342298(asStack_18);
                *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) | 0x2000;
                aveditor_add_to_group(frame, param_2, param_3, param_4, param_5, param_6,
                                      param_7, param_8);
                hud_input_reset_repeat_timers();
                return;
            }
        }
        else {
            screen_enter_confirm(param_2, param_3, param_4, param_5, param_6, param_7, param_8, 0);
            hud_input_reset_repeat_timers();
        }
    }

    if (*(int *)(DAT_80612be4 + 0x1185c) != 6) {
        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        if (((((*(u32 *)(root + 0xc0) & 0x10) == 0) || (*(int *)(DAT_80612be4 + 0x2f70) != 0)) ||
             (*(int *)(DAT_80612be4 + 0x1185c) == 5)) ||
            ((ok = (u32)FUN_80332c04(), ok != 0) ||
             (ok = avanim_layout_is_0x22(*(int *)(DAT_80612be4 + 0x2f34)), ok != 0))) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if (((*(u32 *)(root + 0xc0) & 0x10) == 0) ||
                (ok = avanim_layout_is_0x22(*(int *)(DAT_80612be4 + 0x2f34)), ok == 0)) {
                root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                if (((*(u32 *)(root + 0xc0) & 0x10) == 0) ||
                    ((*(int *)(DAT_80612be4 + 0x1185c) != 5) &&
                     (ok = (u32)FUN_80332c04(), ok == 0))) {
                    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                    int m = *(int *)(DAT_80612be4 + 0x1185c);
                    bool branchA = ((*(u32 *)(root + 0xc0) & 0x40) != 0) &&
                                   ((m == 0) || (m == 1) || ((u32)(m - 3) < 2));
                    if (!branchA) {
                        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
                        m = *(int *)(DAT_80612be4 + 0x1185c);
                    }
                    bool branchB = ((*(u32 *)(root + 0xc0) & 0x20) != 0) &&
                                   ((m == 2) || (m == 5) || (m == 8));
                    if (branchA || branchB) {
                        *(int *)(DAT_80612be4 + 0x2f34) = 0xffffffff;
                        *(int *)(DAT_80612be4 + 0x2f38) = 0xffffffff;
                        *(short *)(DAT_80612be4 + 0x2f10) = (short)0xffff;
                        screen_enter_confirm(param_2, param_3, param_4, param_5, param_6,
                                             param_7, param_8, 0);
                        hud_input_reset_repeat_timers();
                        return;
                    }
                }
                else if (-1 < *(int *)(DAT_80612be4 + 0x2f78)) {
                    hud_input_reset_repeat_timers();
                    slot = DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f78) * 0xc;
                    if (*(short *)(slot + 0x307a) < 0) {
                        cshort = (unsigned short)FUN_80332e00(0x46);
                        *(short *)(slot + 0x307a) = (short)cshort;
                        if (*(short *)(slot + 0x307a) == -1) {
                            screen_enter_confirm(param_2, param_3, param_4, param_5, param_6,
                                                 param_7, param_8, 0);
                        }
                    }
                    {
                        u64 cf = FUN_80332cd8(*(short *)(slot + 0x307a), 0xffff);
                        screen_enter_confirm(cf, param_3, param_4, param_5, param_6, param_7,
                                             param_8, 1);
                    }
                }
            }
            else if (((*(int *)(DAT_80612be4 + 0x2f70) != 0) &&
                      (-1 < *(int *)(DAT_80612be4 + 0x2f74))) &&
                     ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x410000) == 0)) {
                slot = DAT_80612be4 + *(int *)(DAT_80612be4 + 0x2f38) * 0x8c;
                aveditor_place_record();
                *(int *)(DAT_80612be4 + 0x2f34) = 0xffffffff;
                *(int *)(slot + 0xdd90) = 1;
                screen_enter_confirm(param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8, 1);
                hud_input_reset_repeat_timers();
                return;
            }
        }
        else if ((*(int *)(DAT_80612be4 + 0x2f34) < 0) ||
                 ((*(u32 *)(DAT_80612be4 + 0x25c) & 4) == 0)) {
            int m = *(int *)(DAT_80612be4 + 0x1185c);
            if (((*(int *)(DAT_80612be4 + 0x2f70) == 0) &&
                 ((*(int *)(DAT_80612be4 + 0x2f34) < 0 &&
                   (*(int **)(DAT_80612be4 + 0x2f1c) != (int *)0)))) &&
                (((m == 2) && (accept = (FUN_80332a84(**(int **)(DAT_80612be4 + 0x2f1c)) != 0), accept)) ||
                 ((m == 8) && (ok = FUN_80332b90(), ok != 0)))) {
                hud_input_reset_repeat_timers();
                if ((*(int **)(DAT_80612be4 + 0x2f1c))[1] < 0) {
                    u32 cv = FUN_80332e00(**(int **)(DAT_80612be4 + 0x2f1c));
                    *(u32 *)(*(int *)(DAT_80612be4 + 0x2f1c) + 4) = cv;
                    if (*(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 4) == -1) {
                        screen_enter_confirm(param_2, param_3, param_4, param_5, param_6,
                                             param_7, param_8, 0);
                    }
                }
                {
                    u64 cf = FUN_80332cd8((short)*(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 4),
                                          0xffff);
                    screen_enter_confirm(cf, param_3, param_4, param_5, param_6, param_7,
                                         param_8, 1);
                }
            }
            else {
                if ((*(int *)(DAT_80612be4 + 0x2f34) == -1) &&
                    ((*(short *)(DAT_80612be4 + 0x2f10) == -1) &&
                     (*(int *)(DAT_80612be4 + 0x1185c) == -1))) {
                    if (aveditor_cursor_hits_zone() != 0) {
                        return;
                    }
                    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x200) != 0) {
                        return;
                    }
                    screen_enter_dispatch();
                    hud_input_reset_repeat_timers();
                    return;
                }
                avanim_play_move_sound(0x1576);
            }
        }
        else {
            FUN_80342298((short *)&local_20);
            local_30 = local_20;
            local_2c = local_1c;
            ok = FUN_80342698(*(int *)(DAT_80612be4 + 0x2f18), (short *)&local_30);
            if ((ok & 0xff) != 0) {
                slot = *(int *)(DAT_80612be4 + 0x2f38) * 0x8c;
                local_38 = local_20;
                local_34 = local_1c;
                int recBase = DAT_80612be4 + slot;
                roster_try_place_character(
                    frame, 0.0, 0.0, 0.0, 0.0, 0.0, param_7, param_8,
                    (u32)*(int *)(DAT_80612be4 + 0x2f18), (u32)(uintptr_t)&local_38,
                    (int *)(DAT_80612be4 + 0x2f34), -0x7fcf6120, (u32)slot, (u32)DAT_80612be4,
                    in_r9, in_r10);
                // roster_try_place_character (void in shared decl) commits the record;
                // the original branched on its result, always finalising the pick.
                *(int *)(DAT_80612be4 + 0x2f34) = 0xffffffff;
                *(int *)(recBase + 0xdd90) = 1;
                screen_enter_confirm(param_2, param_3, param_4, param_5, param_6, param_7,
                                     param_8, 1);
                *(int *)(DAT_80612be4 + 0x2f34) = 0xffffffff;
                hud_input_reset_repeat_timers();
                return;
            }
            avanim_play_move_sound(0x1576);
        }

        if (((-1 < *(int *)(DAT_80612be4 + 0x2f34)) &&
             ((*(u32 *)(DAT_80612be4 + 0x25c) & 4) != 0)) &&
            (((&DAT_80477c80)[*(int *)(DAT_80612be4 + 0x2f34) * 0x31] & 1) != 0)) {
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x20) != 0) {
                *(int *)(DAT_80612be4 + 0x2f3c) = FUN_80313748(*(int *)(DAT_80612be4 + 0x2f3c), -1, 0);
            }
            root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
            if ((*(u32 *)(root + 0xc0) & 0x80) != 0) {
                *(int *)(DAT_80612be4 + 0x2f3c) = FUN_80313748(*(int *)(DAT_80612be4 + 0x2f3c), 1, 0);
            }
        }
        return;
    }

    // mode 6: bulk-select toggle path
    fmt = (char *)get_string_by_id(0xb66);
    FUN_80387fb8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 DAT_80612be4, fmt, 10, in_r6, in_r7, in_r8, in_r9, in_r10);
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc0) & 0x10) != 0) {
        aveditor_store_cursor_grid_pos();
        root = *(int *)(DAT_80612be4 + 0x2f18);
        if ((root != 0) && ((*(unsigned char *)(root + 0x14) & 2) == 0)) {
            if (aveditor_count_selected_members() < 10) {
                *(unsigned char *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x14) =
                    *(unsigned char *)(*(int *)(DAT_80612be4 + 0x2f18) + 0x14) | 2;
                return;
            }
            hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, 0);
            avanim_play_move_sound(0x1575);
            return;
        }
        if (root == 0) {
            return;
        }
        if ((*(unsigned char *)(root + 0x14) & 2) == 0) {
            return;
        }
        *(unsigned char *)(root + 0x14) = *(unsigned char *)(root + 0x14) & 0xfd;
        return;
    }
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc4) & 0x10) != 0) {
        aselgrid_recompute_visibility();
        return;
    }
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc4) & 0x100000) != 0) {
        if (10 < aveditor_count_selected_members()) {
            hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, 0);
            avanim_play_move_sound(0x1575);
            aselgrid_clear_change_flags();
        }
        if ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x20) == 0) {
            return;
        }
        aselgrid_flush_visibility_flags();
        *(u32 *)(DAT_80612be4 + 0x25c) = *(u32 *)(DAT_80612be4 + 0x25c) & 0xffffffdf;
        return;
    }
    root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
    if ((*(u32 *)(root + 0xc0) & 0x20) == 0) {
        root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0));
        if ((*(u32 *)(root + 0xc0) & 0x40) == 0) {
            return;
        }
        clear_member_select_flags();
        return;
    }
    if (10 < aveditor_count_selected_members()) {
        hud_create_back_dialog(DAT_80612bf4, DAT_80612be4, 0);
        avanim_play_move_sound(0x1575);
        return;
    }
    aveditor_rebuild_group_from_selection();
    screen_enter_confirm(param_2, param_3, param_4, param_5, param_6, param_7, param_8, 1);
    hud_input_reset_repeat_timers();
    (void)cval;
}

// ---------------------------------------------------------------------------
// 0x8030b084  aveditor_can_exit
// ---------------------------------------------------------------------------
u32 aveditor_can_exit(void)
{
    int root;
    u32 result = 0;

    if ((*(u32 *)(DAT_80612be4 + 0x25c) & 0x16004008) == 0) {
        root = ((int (*)(void))&hud_screen_destroy_current)();
        if ((root != 0) &&
            (root = ((int (*)(void))&hud_screen_destroy_current)(),
             (*(u32 *)(root + 0x214) & 4) != 0)) {
            return 0;
        }
        if (((*(u32 *)(DAT_80612be4 + 0x25c) & 0x80) == 0) &&
            (root = hud_screen_find_root(*(int *)(&DAT_8051a820 + DAT_806119ec * 0x1e0)),
             root != DAT_80612bf4)) {
            return 0;
        }
        if (((((*(u32 *)(DAT_80612be4 + 0x25c) & 0x20) == 0) &&
              (FUN_803214c0() == 0)) &&
             (*(int *)(DAT_80612be4 + 0x2f34) == -1)) &&
            (((*(short *)(DAT_80612be4 + 0x2f10) == -1) &&
              (*(int *)(DAT_80612be4 + 0x2f7c) == 0)) &&
             (*(int *)(DAT_80612be4 + 0x1185c) == -1))) {
            result = 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8030b174  aveditor_dispatch_input
// ---------------------------------------------------------------------------
void aveditor_dispatch_input(u64 param_1, double param_2, double param_3, double param_4,
                             double param_5, double param_6, u64 param_7, u64 param_8,
                             int param_9, u32 param_10, u32* param_11, int param_12,
                             int param_13, int param_14, u32 param_15, u32 param_16)
{
    int  mode;
    u32  ok;

    mode = *(int *)(DAT_80612be4 + 0x254);
    if (mode == 2) {
        aveditor_create_cell_at_cursor(param_1, (u64)param_2, (u64)param_3, (u64)param_4,
                                       (u64)param_5, (u64)param_6, param_7, param_8);
    }
    else if (mode < 2) {
        if (mode == 0) {
            aveditor_handle_edit_input(param_1, (u64)param_2, (u64)param_3, (u64)param_4,
                                       (u64)param_5, (u64)param_6, param_7, param_8, param_9);
        }
        else if (-1 < mode) {
            if (*(int *)(DAT_80612be4 + 0x1185c) == 8) {
                aveditor_handle_zone_input(param_1, (u64)param_2, (u64)param_3, (u64)param_4,
                                           (u64)param_5, (u64)param_6, param_7, param_8);
            }
            else {
                if (((aveditor_handle_roster_input() == 0) &&
                     (ok = avanim_layout_is_0x22(*(int *)(DAT_80612be4 + 0x2f34)), ok == 0)) &&
                    (ok = roster_can_add(), ok != 0)) {
                    aveditor_handle_place_input(param_1, (u64)param_2, (u64)param_3, (u64)param_4,
                                                (u64)param_5, (u64)param_6, param_7, param_8,
                                                param_9);
                }
            }
        }
    }
    else if (mode == 4) {
        aveditor_handle_zone_input(param_1, (u64)param_2, (u64)param_3, (u64)param_4,
                                   (u64)param_5, (u64)param_6, param_7, param_8);
    }
    else if (mode < 4) {
        aveditor_handle_group_input();
    }
}

// ---------------------------------------------------------------------------
// 0x8030b23c  aveditor_check_add_allowed
// ---------------------------------------------------------------------------
u32 aveditor_check_add_allowed(int param_1)
{
    if (*(char *)(DAT_80612be4 + 0x11854) == '\x01') {
        return FUN_8032196c(param_1);
    }
    return 1;
}
