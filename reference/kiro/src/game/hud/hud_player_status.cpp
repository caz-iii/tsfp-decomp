// ============================================================================
// Address range: 0x80365eac - 0x80366038 | batch_1027 | 2026-09-12
//
// HUD player-status text formatter plus a couple of small frame/input glue
// helpers that live immediately after the ragdoll rig module. The centrepiece
// is hud_format_player_status, which builds the nametag / scoreline string that
// hud_nametag_render draws above a character and that the player-character
// system uses at spawn. Three text layouts are selected by game mode:
//
//   * team / objective mode : "%s %d : %s"  (name, number, status string)
//   * a special first-slot   : "[%s] %s"     (tag, status string)
//   * default                : "%s"          (status string only)
//
//   hud_status_stub_zero    0x80365eac  predicate stub, always returns 0
//   hud_format_player_status 0x80365eb4 build the player nametag/scoreline text
//   player_get_status_block  0x80365fd8 fetch a player's status-string block
//   frame_input_update       0x80366010 per-frame input poll (input_update_all)
//   ret0_stub                0x80366030 stub, returns 0
//   empty_stub               0x80366038 stub, no-op
//
// NOTE: non-matching decomp. FUN_80387fb8 is the engine's formatted-string
// builder (a vsnprintf-style helper); the original threads its many register
// args straight through, so it is declared permissively and called with the
// output buffer, format string, and the substitution values that matter.
// ============================================================================

#include "common.h"
#include "generated_decls.h"

// ---- format strings (rodata) ----
extern char s__s__d____s_804942e0[];   // "%s %d : %s"
extern char DAT_806112c0[];             // "[%s] %s"
extern char DAT_806112c8[];             // "%s"

// ---- mode flag global ----
extern int DAT_80612e90;   // 1 when the team/objective scoreline mode is active

// ---- per-player status-string base table (indexed by player-slot * 0x20) ----
extern int DAT_805eba1c;   // player status record base: score/rank value word

// raw engine helpers not yet decompiled.
extern int  FUN_80366414(void);              // team/objective-mode active flag
extern void FUN_80387fb8(int outBuf, int fmt, int a3, int a4, int a5);  // format builder

// register spill-frame accessor: the player-slot index arrives in the r28 spill
// (same idiom the surrounding modules use); no-op restore.
extern int  __save_gpr_28(void);
static inline void __restore_gpr_28(void) {}

// -------------------------------------------------------------------------
// 0x80365eac  hud_status_stub_zero - predicate stub, always returns 0.
//   Used by hud_format_player_status as the "no bracket tag" default.
// -------------------------------------------------------------------------
int hud_status_stub_zero(void)
{
    return 0;
}

// -------------------------------------------------------------------------
// 0x80365eb4  hud_format_player_status - build the player nametag/scoreline.
//   Chooses one of three text layouts based on the scoreline mode flag and the
//   player's score value, writing into the caller-supplied output buffer:
//     team/objective, score > 0 : "%s %d : %s"  (name, score, status text)
//     special slot 0            : "[%s] %s"      (bracket tag, status text)
//     default                   : "%s"           (status text only)
//   The status text is the block returned by player_get_status_block; the
//   "%s %d : %s" middle string is a localized label (string id 0x25).
//   Callers: hud_nametag_render, player_character_system_init.
// -------------------------------------------------------------------------
void hud_format_player_status(int outBuf)
{
    int slot = __save_gpr_28();
    int scoreVal = *(int*)(&DAT_805eba1c + slot * 0x118);
    int statusText = hud_status_stub_zero();

    if (FUN_80366414() != 0) {
        if (0 < scoreVal) {
            statusText = player_get_status_block(slot);
            int label = get_string_by_id(0x25);
            FUN_80387fb8(outBuf, (int)s__s__d____s_804942e0, label, scoreVal, statusText);
            __restore_gpr_28();
            return;
        }
        if ((statusText != 0) && (slot == 0)) {
            int tag = player_get_status_block(0);
            FUN_80387fb8(outBuf, (int)DAT_806112c0, statusText, tag, 0);
            __restore_gpr_28();
            return;
        }
    }

    statusText = player_get_status_block(slot);
    FUN_80387fb8(outBuf, (int)DAT_806112c8, statusText, slot * 0x20, 0);
    __restore_gpr_28();
}

// -------------------------------------------------------------------------
// 0x80365fd8  player_get_status_block - fetch a player's status-string block.
//   Looks up the player slot's data record; returns a pointer to its status
//   block (record + 4), or 0 when the slot has no data.
// -------------------------------------------------------------------------
int player_get_status_block(int slot)
{
    int* rec = (int*)player_slot_get_data(slot);
    if (rec == (int*)0) {
        return 0;
    }
    return *rec + 4;
}

// -------------------------------------------------------------------------
// 0x80366010  frame_input_update - per-frame input poll.
//   Thin wrapper the frame render loop calls to sample all input devices.
//   Caller: frame_render_update.
// -------------------------------------------------------------------------
void frame_input_update(void)
{
    input_update_all();
}

// -------------------------------------------------------------------------
// 0x80366030  ret0_stub - stub, returns 0.
// -------------------------------------------------------------------------
int ret0_stub(void)
{
    return 0;
}

// -------------------------------------------------------------------------
// 0x80366038  empty_stub - stub, no-op.
// -------------------------------------------------------------------------
void empty_stub(void)
{
}
