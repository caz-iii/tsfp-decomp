// Batch: batch_873 | Module: arcade map-editor delete/remove actions + dispatcher
// Address range: 0x80307994 - 0x80307cec
// Date: 2026-09-11
//
// The delete / remove / clear action callbacks for the arcade map editor, the
// confirm-dialog wrappers that run them, and the action-selector dispatcher that
// batch_872's action_confirm_dialog uses to pick which callback to arm. All hang
// off the active screen ctx DAT_80612be4 and the global member list DAT_80612c14.
//
//   delete_picked_cell        0x80307994  SFX 0x1577, mark dirty, detach the picked
//                                         cell (FUN_80314040 over ctx+0x2f18/+0x2f1c),
//                                         clear the picked-cell ptr, reset input.
//   remove_roster_member      0x80307a00  roster_remove_and_clear(ctx+0x2f78) + reset.
//   confirm_delete_cell       0x80307a2c  dialog confirm: set [p+0x188]=1, close dialog
//                                         (FUN_80338d14), then delete_picked_cell.
//   confirm_clear_selection   0x80307a68  dialog confirm -> clear_selection_type.
//   confirm_remove_roster     0x80307aa4  dialog confirm -> remove_roster_member.
//   resolve_delete_action     0x80307ae0  inspect the current selection/paste/roster
//                                         state and return the matching action callback
//                                         (clear/remove/delete or their confirm variant
//                                         when FUN_80338bd4 says a confirm is needed),
//                                         or clear_all_action / delete_member_action for
//                                         the paste-buffer / picked-member cases.
//   show_blocked_dialog       0x80307bf4  back dialog with string 0x95b + SFX 0x1575.
//   clear_all_action          0x80307c3c  SFX, free the paste chain (FUN_80313cf4),
//                                         roster_clear_all(ctx+0x3868), FUN_80338378.
//   delete_member_action      0x80307c7c  mark dirty (mode 4), SFX, free the picked
//                                         member (FUN_80313bd4) or the flagged members
//                                         (aselgrid_free_flagged_members) per its bit
//                                         0x2000000, FUN_80338378.
//   clear_member_select_flags 0x80307cec  walk the member list (DAT_80612c14, next
//                                         @+0x40) clearing the select/highlight bits
//                                         0x2 / 0x4 in each node's flags byte @+0x14.
//
// NON-MATCHING: resolve_delete_action returns raw function pointers; its result type
// is undefined* in the decompiler but is really a "void (*)(...)" action callback -
// modeled here as void* so the address-identity chain (shared with batch_872's
// action_confirm_dialog) stays intact. clear_selection_type (batch_872) resolves via
// the shared header. Named cross-module helpers (avanim_play_move_sound,
// roster_remove_and_clear, roster_clear_all, get_string_by_id, hud_create_back_dialog,
// aselgrid_free_flagged_members, hud_input_reset_repeat_timers) resolve through the
// shared headers; every FUN_/DAT_ raw symbol gets a per-file extern below.

#include "common.h"
#include "hud.h"

// Raw per-file symbol externs use C linkage; the batch's own DEFINITIONS use C++
// linkage (below, outside this block) to match generated_decls.h.
extern "C" {

// --- raw FUN_ helpers referenced from this module ---
void FUN_80348238(int mode);               // mark ctx dirty (sound/anim)
void FUN_80314040(int listHead, int cell); // detach one cell from a member
void FUN_80338d14(void);                   // close the active dialog
int  FUN_80338bd4(void);                   // confirm-required predicate (-1 = no confirm)
void FUN_80313cf4(float* pMember);         // free a member chain
void FUN_80338378(void);                   // post-delete screen refresh
void FUN_80313bd4(float* pMember);         // free a single member node

// --- active screen context + globals ---
extern int DAT_80612be4;   // active screen context base
extern int DAT_80612c14;   // head of the current member linked list

} // extern "C"

// forward decls (address-identity + intra-file references)
void delete_picked_cell(void);
void remove_roster_member(void);
void confirm_delete_cell(int param_1);
void confirm_clear_selection(int param_1);
void confirm_remove_roster(int param_1);
void clear_all_action(void);
void delete_member_action(void);

// ---------------------------------------------------------------------------
// 0x80307994  delete_picked_cell
// ---------------------------------------------------------------------------
void delete_picked_cell(void)
{
    avanim_play_move_sound(0x1577);
    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(0);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
    }
    FUN_80314040(*(int *)(DAT_80612be4 + 0x2f18), *(int *)(DAT_80612be4 + 0x2f1c));
    *(int *)(DAT_80612be4 + 0x2f1c) = 0;
    hud_input_reset_repeat_timers();
}

// ---------------------------------------------------------------------------
// 0x80307a00  remove_roster_member
// ---------------------------------------------------------------------------
void remove_roster_member(void)
{
    roster_remove_and_clear(*(int *)(DAT_80612be4 + 0x2f78));
    hud_input_reset_repeat_timers();
}

// ---------------------------------------------------------------------------
// 0x80307a2c  confirm_delete_cell
// ---------------------------------------------------------------------------
void confirm_delete_cell(int param_1)
{
    *(int *)(param_1 + 0x188) = 1;
    FUN_80338d14();
    delete_picked_cell();
}

// ---------------------------------------------------------------------------
// 0x80307a68  confirm_clear_selection
// ---------------------------------------------------------------------------
void confirm_clear_selection(int param_1)
{
    *(int *)(param_1 + 0x188) = 1;
    FUN_80338d14();
    clear_selection_type();
}

// ---------------------------------------------------------------------------
// 0x80307aa4  confirm_remove_roster
// ---------------------------------------------------------------------------
void confirm_remove_roster(int param_1)
{
    *(int *)(param_1 + 0x188) = 1;
    FUN_80338d14();
    remove_roster_member();
}

// ---------------------------------------------------------------------------
// 0x80307ae0  resolve_delete_action
// Inspect the current selection/paste/roster state and return the action callback
// to arm (or its confirm variant when FUN_80338bd4 requires a confirmation).
// ---------------------------------------------------------------------------
void* resolve_delete_action(void)
{
    void* cb;
    int   confirm;

    if ((*(int *)(DAT_80612be4 + 0x2f7c) == 0) || (*(int *)(DAT_80612be4 + 0x254) != 0)) {
        if ((*(int *)(DAT_80612be4 + 0x2f18) == 0) || (*(int *)(DAT_80612be4 + 0x254) != 0)) {
            if (*(int *)(DAT_80612be4 + 0x2f1c) == 0) {
                if ((-1 < *(int *)(DAT_80612be4 + 0x2f38)) &&
                    (-1 < *(int *)(DAT_80612be4 + 0x2f34))) {
                    confirm = FUN_80338bd4();
                    if (confirm == -1) {
                        return (void *)clear_selection_type;
                    }
                    return (void *)confirm_clear_selection;
                }
                if (-1 < *(short *)(DAT_80612be4 +
                                    *(int *)(DAT_80612be4 + 0x2f78) * 0xc + 0x307a)) {
                    confirm = FUN_80338bd4();
                    if (confirm == -1) {
                        return (void *)remove_roster_member;
                    }
                    return (void *)confirm_remove_roster;
                }
            }
            else if (-1 < *(int *)(*(int *)(DAT_80612be4 + 0x2f1c) + 4)) {
                confirm = FUN_80338bd4();
                if (confirm == -1) {
                    return (void *)delete_picked_cell;
                }
                return (void *)confirm_delete_cell;
            }
            cb = (void *)0x0;
        }
        else {
            cb = (void *)delete_member_action;
        }
    }
    else {
        cb = (void *)clear_all_action;
    }
    return cb;
}

// ---------------------------------------------------------------------------
// 0x80307bf4  show_blocked_dialog
// ---------------------------------------------------------------------------
void show_blocked_dialog(int param_1)
{
    int msg;

    msg = get_string_by_id(0x95b);
    hud_create_back_dialog(param_1, msg, 0);
    avanim_play_move_sound(0x1575);
}

// ---------------------------------------------------------------------------
// 0x80307c3c  clear_all_action
// ---------------------------------------------------------------------------
void clear_all_action(void)
{
    avanim_play_move_sound(0x1577);
    FUN_80313cf4(*(float **)(DAT_80612be4 + 0x2f7c));
    roster_clear_all(DAT_80612be4 + 0x3868);
    FUN_80338378();
}

// ---------------------------------------------------------------------------
// 0x80307c7c  delete_member_action
// ---------------------------------------------------------------------------
void delete_member_action(void)
{
    if ((*(u32 *)(DAT_80612be4 + 0x238) & 0x20) == 0) {
        FUN_80348238(4);
        *(u32 *)(DAT_80612be4 + 0x238) = *(u32 *)(DAT_80612be4 + 0x238) | 0x20;
    }
    avanim_play_move_sound(0x1577);
    if (((u32)(*(float **)(DAT_80612be4 + 0x2f18))[5] & 0x2000000) == 0) {
        FUN_80313bd4(*(float **)(DAT_80612be4 + 0x2f18));
    }
    else {
        aselgrid_free_flagged_members();
    }
    FUN_80338378();
}

// ---------------------------------------------------------------------------
// 0x80307cec  clear_member_select_flags
// ---------------------------------------------------------------------------
void clear_member_select_flags(void)
{
    int node;

    node = DAT_80612c14;
    while (true) {
        if (node == 0) break;
        if ((*(unsigned char *)(node + 0x14) & 2) != 0) {
            *(unsigned char *)(node + 0x14) = *(unsigned char *)(node + 0x14) & 0xfd;
        }
        if ((*(unsigned char *)(node + 0x14) & 4) != 0) {
            *(unsigned char *)(node + 0x14) = *(unsigned char *)(node + 0x14) & 0xfb;
        }
        node = *(int *)(node + 0x40);
    }
}
