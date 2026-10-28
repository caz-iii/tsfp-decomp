// Address: 0x802daab4 | Name: menu_profile_confirm_screen | Date: 2026-09-11T07:05:00Z
//
// Player-profile edit / confirm screen callbacks (batch_822).
// These small handlers implement the "edit profile options" flow: copy the
// active player's savegame block into a scratch edit block, drive the option
// screens, detect whether the user changed anything, and either commit the
// changes back (with an audio cue) or cancel. A memcard save session is kicked
// off when the profile is confirmed.
//
// Address range: 0x802daab4 - 0x802dad24

#include "common.h"

// ---- Per-file externs for raw symbols referenced by this module ------------

// Savegame / profile block accessors and layout.
extern uint  savegame_get_player_block(int slot);   // returns block base pointer
extern int   DAT_805cc61c;                           // active savegame slot id
extern int   DAT_806119ec;                           // active profile index (row)
extern u8    DAT_8051a918[];                          // profile default template table (0x1e0 stride)
extern u32   DAT_805cc60c;                            // profile dirty / state flags
extern u32   DAT_80612bc4;                            // "changed since last save" latch
extern int   DAT_80612ac4;                            // memcard session slot handle
extern void *PTR_FUN_80476600;                        // memcard session callback vtable

// Scratch block copy helper (memmove-style: dst, src, len).
extern void  leaf_31e8(uint dst, uint src, uint len);

// Screen stack helpers (resolved via shared headers, declared here for safety).
extern void  hud_screen_push(void);
extern void  hud_screen_pop_with_restore(void);

// Option-screen setup helpers.
extern void  FUN_802f5f3c(void);
extern void  FUN_802f5140(void);
extern void  menu_options_settings_load(void);

// Sound playback.
extern void  sound_init_play_params(int params);
extern void  sound_play_request(int *params);

// Memcard session.
extern void  memcard_session_begin_slot(int slot, void *callbacks);

// ---- Profile edit block layout (offsets into the savegame player block) ----
// off 0x02 : u16 profile flags/id
// off 0x68 : s32 stat A
// off 0x74 : s32 stat B
// off 0x78 : f32 stat C
// The default template row lives at DAT_8051a918 + index*0x1e0, mirroring the
// same field offsets (0x02, 0x68, 0x74, 0x78 relative to the row base).
#define PROFILE_TEMPLATE_STRIDE 0x1e0
#define PROFILE_BLOCK_COPY_LEN  0x7c
#define PROFILE_FLAG_EDITING    0x10000

// ---------------------------------------------------------------------------

// 0x802daab4 - Begin editing: copy the current profile block into the shared
// edit template row, mark the profile as being edited, then open the screen.
void menu_profile_edit_begin(void)
{
    uint blockPtr = savegame_get_player_block(DAT_805cc61c);
    uint templateRow = (uint)(&DAT_8051a918[DAT_806119ec * PROFILE_TEMPLATE_STRIDE]);

    leaf_31e8(blockPtr, templateRow, PROFILE_BLOCK_COPY_LEN);
    DAT_805cc60c = DAT_805cc60c | PROFILE_FLAG_EDITING;
    hud_screen_push();
}

// 0x802dab24 - Build the profile options screen: run the two setup passes then
// push it onto the screen stack.
void menu_profile_options_screen_build(void)
{
    FUN_802f5f3c();
    FUN_802f5140();
    hud_screen_push();
}

// 0x802dab58 - Compare the live profile block against the default template row.
// Returns 0 when identical (nothing changed), 1 when the user modified a field.
int menu_profile_settings_changed(void)
{
    int blockPtr = (int)savegame_get_player_block(DAT_805cc61c);
    int rowOff = DAT_806119ec * PROFILE_TEMPLATE_STRIDE;

    if ((*(short *)(blockPtr + 0x02) == *(short *)(&DAT_8051a918[rowOff + 0x02])) &&
        (*(int   *)(blockPtr + 0x74) == *(int   *)(&DAT_8051a918[rowOff + 0x74])) &&
        (*(int   *)(blockPtr + 0x68) == *(int   *)(&DAT_8051a918[rowOff + 0x68])) &&
        (*(float *)(blockPtr + 0x78) == *(float *)(&DAT_8051a918[rowOff + 0x78]))) {
        return 0;
    }
    return 1;
}

// 0x802dabe4 - Re-enter the profile editor (thin wrapper).
void menu_profile_edit_reenter(void)
{
    menu_profile_edit_begin();
}

// 0x802dac04 - Cancel: pop the screen and restore the previous state.
void menu_profile_cancel(void)
{
    hud_screen_pop_with_restore();
}

// 0x802dac28 - Reset the editor back to the profile defaults (thin wrapper).
void menu_profile_edit_reset(void)
{
    menu_profile_edit_begin();
}

// 0x802dac48 - Build the profile settings screen: load persisted settings, push.
void menu_profile_settings_screen_build(void)
{
    menu_options_settings_load();
    hud_screen_push();
}

// 0x802dac78 - Build a simple (no-setup) profile screen: just push it.
void menu_profile_simple_screen_build(void)
{
    hud_screen_push();
}

// 0x802daca4 - Commit the edited profile back into the savegame block, clear
// the changed latch, play the confirmation sound cue, then open the screen.
void menu_profile_commit_and_confirm(void)
{
    int soundParams[14];

    uint blockPtr = savegame_get_player_block(DAT_805cc61c);
    uint templateRow = (uint)(&DAT_8051a918[DAT_806119ec * PROFILE_TEMPLATE_STRIDE]);

    // Note the swapped argument order versus edit_begin: here we copy the
    // edited template row back into the live profile block.
    leaf_31e8(templateRow, blockPtr, PROFILE_BLOCK_COPY_LEN);
    DAT_80612bc4 = 0;

    sound_init_play_params((int)soundParams);
    soundParams[0] = 0xd1e;              // confirmation SFX id
    sound_play_request(soundParams);

    hud_screen_push();
}

// 0x802dad24 - Kick off a memcard save session for the confirmed profile.
void menu_profile_memcard_save_begin(void)
{
    memcard_session_begin_slot(DAT_80612ac4, (void *)&PTR_FUN_80476600);
}
