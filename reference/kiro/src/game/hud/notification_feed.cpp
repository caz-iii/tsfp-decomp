// =============================================================================
// notification_feed.cpp - Kill feed / event notification system
// Address Range: 0x8004f458 - 0x8004fcb0
// Date: 2026-08-10
// Batch: 103
//
// Manages the on-screen event notification feed (kill messages, score events,
// zone captures, team announcements, etc.). Supports up to 4 simultaneous
// notification slots (one per viewport/player in split-screen). Each slot has
// a state machine: 0=inactive, 1=active/set, 2=fading out, 3=fading with callback.
//
// Notification types (0x00-0x2E+) map to localized format strings via string IDs.
// Player names are resolved from the notification context player slot.
// =============================================================================

#include "notification_feed.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// Notification slot array: 4 entries, stride 0x224 (0x89 dwords each)
// Layout per slot:
//   +0x000: state (0=inactive, 1=pending, 2=fade-out, 3=fade-out+callback)
//   +0x004: HUD screen handle
//   +0x00C: fade timer (float)
static NotificationSlot g_notification_slots[4]; // DAT_805272b8

// Active flag: 1 = notification system is enabled
static int g_notification_active; // DAT_80611a80

// Current notification player slot index (-1 = none)
static int g_notification_player_slot; // DAT_80611a90

// Related player data index (-1 = none)
static int g_notification_player_data; // DAT_80611a8c

// Notification context: stores current event type and player name buffer
static int g_notification_event_type; // DAT_806103f0

// Empty string fallback
static const char g_empty_string[] = ""; // DAT_806103f4

// Format string buffer
static char g_format_buffer[512]; // DAT_805270b8

// Player name buffers (stride 0x40, indexed by player data slot)
static char g_player_name_buffers[8][64]; // DAT_80527b48

// ---------------------------------------------------------------------------
// Address: 0x8004f458 | Name: notif_get_controller_mode_name
//
// Returns the localized name of the current controller mode (0-4) for use
// in notification messages. Falls back to empty string if out of range.
// ---------------------------------------------------------------------------
const char* notif_get_controller_mode_name(void)
{
    int mode = notif_get_player_slot(); // FUN_80050704

    if (mode < 0 || mode > 4) {
        return g_empty_string;
    }

    // Lookup from stride-0x14 controller mode name table
    int string_id = g_controller_mode_table[mode].name_string_id; // DAT_803fe928
    return (const char*)get_string_by_id(string_id);
}

// ---------------------------------------------------------------------------
// Address: 0x8004f4b0 | Name: notif_format_event_message
//
// MASSIVE: Formats the notification message text for ~30+ event types.
// Dispatches on the current notification event type to build a formatted
// string with player names, button prompts, and localized text.
//
// Event types include:
//   0x00: Generic notification (string 0xe0f)
//   0x01: Player killed (with killer name, string 0xe10)
//   0x02: Player killed by (with victim name, string 0xe11)
//   0x03: Suicide (string 0xe49)
//   0x04: Headshot (string 0xe12)
//   0x05: Burn kill (string 0xe13)
//   0x06-0x07: Special kill (string 0xe15)
//   0x08: Team kill warning (string 0xe16)
//   0x09: Score event (with player name, string 0x3a6)
//   0x0A: Flag captured (string 0xe17)
//   0x0B: Zone secured (string 0xe19)
//   0x0C: Zone lost (string 0xe18)
//   0x0D: Bag grabbed (string 0xe1a)
//   0x0E: Bag scored (string 0xe1b)
//   0x0F: Lives remaining (with player name, string 0x3a1)
//   0x10: Last life (with player name, string 0x3a0)
//   0x11: Infection spread (with player name, string 0x3a3)
//   0x12: Cured (with player name, string 0x3a2)
//   0x13: Challenge complete (with player name, string 0xe0c)
//   0x14: Match point (string 0x19b3)
//   0x15: Round won (with player name, string 0xe1c)
//   0x16: Round lost (with player name, string 0xe1d)
//   0x17: Assist kill (with 2 player names, string 0x3a5)
//   0x18: Revenge kill (with 2 player names, string 0x3a4)
//   0x19: Press cancel to exit (string 0xa6d)
//   0x1A: Rank display (with rank char, string 0x1943)
//   0x1B: Score value (string 0xa79)
//   0x1C: Weapon pickup (with player name, string 0xa6e)
//   0x1D: Default/unknown (string 0xe20)
//   0x1F: Kill streak (with player name, string 0x39e)
//   0x20: Kills needed (with rank+char, string 0x3c7)
//   0x25: Domination (with player name, string 0x39d)
//   0x27: Multi-kill (with player name + count, string 0x39f)
//   0x28: Rampage (with player name + count, string 0xa74)
//   0x29: Spree ended (with player name, string 0xa72)
//   0x2A: Shutdown (with player name + count, string 0xa76)
//   0x2B: Cancel button prompt (input_get_cancel_button_name)
//   0x2C: Action button prompt (input_get_action_button_name)
//   0x2D: Special button prompt (with player name, input_get_special_button_name)
//   0x2E: Controller mode display (notif_get_controller_mode_name + player name)
// ---------------------------------------------------------------------------
void notif_format_event_message(void)
{
    int event_type = notif_text_begin(); // FUN_803826f0 - gets event type

    switch (event_type) {
    case 0: {
        const char* text = (const char*)get_string_by_id(0xe0f);
        notif_text_set_simple(g_format_buffer, text, 0x200); // FUN_8038b4cc
        break;
    }
    case 1: {
        const char* player_name = notif_get_player_name(); // FUN_80050744
        const char* fmt = (const char*)get_string_by_id(0xe10);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 2: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xe11);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 3: {
        const char* text = (const char*)get_string_by_id(0xe49);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 4: {
        const char* text = (const char*)get_string_by_id(0xe12);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 5: {
        const char* text = (const char*)get_string_by_id(0xe13);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 6:
    case 7: {
        const char* text = (const char*)get_string_by_id(0xe15);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 8: {
        const char* fmt = (const char*)get_string_by_id(0xe16);
        notif_text_set_formatted(g_format_buffer, fmt, 0x80520000); // FUN_80387fb8
        break;
    }
    case 9: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a6);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 10: {
        const char* text = (const char*)get_string_by_id(0xe17);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 11: {
        const char* text = (const char*)get_string_by_id(0xe19);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 12: {
        const char* text = (const char*)get_string_by_id(0xe18);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 13: {
        const char* text = (const char*)get_string_by_id(0xe1a);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 14: {
        const char* text = (const char*)get_string_by_id(0xe1b);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 15: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a1);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 16: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a0);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 17: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a3);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 18: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a2);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 19: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xe0c);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 20: {
        const char* text = (const char*)get_string_by_id(0x19b3);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 21: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xe1c);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 22: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xe1d);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 23: {
        // Assist kill: 2 player names
        const char* player_name_a = notif_get_player_name();
        const char* player_name_b = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a5);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_2arg, player_name_b, player_name_a);
        break;
    }
    case 24: {
        // Revenge kill: 2 player names
        const char* player_name_a = notif_get_player_name();
        const char* player_name_b = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x3a4);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_2arg, player_name_b, player_name_a);
        break;
    }
    case 25: {
        const char* text = (const char*)get_string_by_id(0xa6d);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 26: {
        int rank_data = notif_get_player_data(); // FUN_8005071c
        const char* fmt = (const char*)get_string_by_id(0x1943);
        notif_text_set_formatted(g_format_buffer, fmt, rank_data + 0x41);
        break;
    }
    case 27: {
        const char* fmt = (const char*)get_string_by_id(0xa79);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, /* score value */);
        break;
    }
    case 28: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xa6e);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    default: {
        const char* text = (const char*)get_string_by_id(0xe20);
        notif_text_set_simple(g_format_buffer, text, 0x200);
        break;
    }
    case 31: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x39e);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 32: {
        int rank_data = notif_get_player_data();
        const char* fmt = (const char*)get_string_by_id(0x3c7);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_rank, rank_data + 0x41, 0x82);
        break;
    }
    case 37: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x39d);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 38:
        // No-op (0x26)
        break;
    case 39: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0x39f);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_2arg, /* count */, player_name);
        break;
    }
    case 40: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xa74);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_2arg, /* count */, player_name);
        break;
    }
    case 41: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xa72);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_1arg, /* count */, player_name);
        break;
    }
    case 42: {
        const char* player_name = notif_get_player_name();
        const char* fmt = (const char*)get_string_by_id(0xa76);
        format_string_varargs(g_format_buffer, fmt, &g_notif_fmt_2arg, /* count */, player_name);
        break;
    }
    case 43: {
        const char* btn = (const char*)input_get_cancel_button_name();
        notif_text_set_simple(g_format_buffer, btn, 0x200);
        break;
    }
    case 44: {
        const char* btn = (const char*)input_get_action_button_name();
        notif_text_set_simple(g_format_buffer, btn, 0x200);
        break;
    }
    case 45: {
        const char* player_name = notif_get_player_name();
        const char* btn = (const char*)input_get_special_button_name();
        format_string_varargs(g_format_buffer, btn, &g_notif_fmt_1arg, player_name);
        break;
    }
    case 46: {
        const char* player_name = notif_get_player_name();
        const char* mode_name = (const char*)notif_get_controller_mode_name();
        format_string_varargs(g_format_buffer, mode_name, &g_notif_fmt_1arg, player_name);
        break;
    }
    }

    notif_text_end(); // FUN_8038273c
}

// ---------------------------------------------------------------------------
// Address: 0x8004fb9c | Name: notif_stub_fb9c
//
// Empty stub - reserved slot in notification vtable.
// ---------------------------------------------------------------------------
void notif_stub_fb9c(void)
{
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x8004fba0 | Name: notif_stub_fba0
//
// Empty stub - reserved slot in notification vtable.
// ---------------------------------------------------------------------------
void notif_stub_fba0(void)
{
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x8004fba4 | Name: notif_system_init
//
// Initializes the notification feed system. Sets the active flag and
// clears all 4 notification slots for multiplayer game modes.
// Only clears slots in modes 0x65-0x66 (arcade), 0x6A (assault),
// 0x6B (elimination) - i.e. competitive multiplayer modes.
// ---------------------------------------------------------------------------
void notif_system_init(void)
{
    g_notification_active = 1;
    notif_set_event_type(1); // FUN_800506f0

    int game_mode = g_current_game_mode; // DAT_80612614
    if ((game_mode - 0x65u < 2) || game_mode == 0x6a || game_mode == 0x6b) {
        for (int i = 0; i < 4; i++) {
            g_notification_slots[i].state = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8004fc14 | Name: notif_reset_player_slot
//
// Resets the notification context: clears both player slot indices to -1
// (no player) and sets event type to 1 (default/ready).
// ---------------------------------------------------------------------------
void notif_reset_player_slot(void)
{
    g_notification_player_slot = -1; // 0xFFFFFFFF
    g_notification_player_data = -1; // 0xFFFFFFFF
    notif_set_event_type(1);
}

// ---------------------------------------------------------------------------
// Address: 0x8004fc44 | Name: notif_dismiss_all
//
// Triggers fade-out dismissal of notifications across all 4 player viewports.
// Only operates in multiplayer modes (0x65-0x66, 0x6A, 0x6B).
// Calls FUN_800505e4 per slot which handles the fade-out state machine:
// state 2 = fade down + pop screen, state 3 = fade down + pop + callback.
// ---------------------------------------------------------------------------
void notif_dismiss_all(void)
{
    int game_mode = g_current_game_mode; // DAT_80612614
    if ((game_mode - 0x65u < 2) || game_mode == 0x6a || game_mode == 0x6b) {
        for (int i = 0; i < 4; i++) {
            notif_slot_fade_update(i); // FUN_800505e4
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8004fca0 | Name: notif_stub_fca0
//
// Empty stub - no-op callback placeholder.
// ---------------------------------------------------------------------------
void notif_stub_fca0(void)
{
    return;
}

// ---------------------------------------------------------------------------
// Address: 0x8004fca4 | Name: notif_set_active
//
// Sets the notification system active flag to 1 (enabled).
// Called when entering gameplay to ensure notifications are shown.
// ---------------------------------------------------------------------------
void notif_set_active(void)
{
    g_notification_active = 1;
}

// ---------------------------------------------------------------------------
// Address: 0x8004fcb0 | Name: notif_is_visible
//
// Returns whether the current viewport's notification slot is active/visible.
// Checks if the state field (at slot[viewport_index].state) is non-zero.
// Uses g_active_viewport_index (DAT_806119ec) to select the correct slot.
// ---------------------------------------------------------------------------
bool notif_is_visible(void)
{
    return g_notification_slots[g_active_viewport_index].state != 0;
}

// =============================================================================
// Notification Popup / Dialog System (Batch 104)
// Address Range: 0x8004fcd0 - 0x80050088
// Date: 2026-08-10
//
// Manages the notification slot lifecycle: allocation, closing, dismissal,
// and popup dialog creation (back/confirm and yes/no prompts).
// Each notification slot (stride 0x224) contains:
//   +0x000: state (0=inactive, 1=active, 2=fading, 3=fading+callback)
//   +0x004: HUD screen handle
//   +0x007: null terminator flag
//   +0x008: text buffer (0x1FF chars)
//   +0x010: saved element flags
//   +0x014: completion callback A
//   +0x018: completion callback B
//   +0x01C: additional callback param
//   +0x204: slot index cache
//   +0x208: source element handle
//   +0x214: callback A (function ptr)
//   +0x218: callback B (function ptr)
// =============================================================================

// ---------------------------------------------------------------------------
// Address: 0x8004fcd0 | Name: notif_slot_close
//
// Closes a notification slot: sets state to 0 (inactive) and invokes
// the completion callback A (at offset +0x14 in the slot) if non-null.
// param_1: HUD element pointer (slot index extracted from +0x38)
// ---------------------------------------------------------------------------
void notif_slot_close(int param_1)
{
    int slot_idx = *(int*)(param_1 + 0x38);

    // Clear slot state
    g_notification_slots[slot_idx].state = 0;

    // Invoke completion callback A if set
    void (*callback)(void) = g_notification_slots[slot_idx].callback_a;
    if (callback != NULL) {
        callback();
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8004fd1c | Name: notif_slot_close_with_clear
//
// Clears HUD dirty flags on the element, then closes the notification slot.
// Used when the notification is dismissed through normal UI flow.
// ---------------------------------------------------------------------------
void notif_slot_close_with_clear(int param_1)
{
    hud_screen_clear_dirty_flags(param_1);
    notif_slot_close(param_1);
}

// ---------------------------------------------------------------------------
// Address: 0x8004fd50 | Name: notif_slot_close_alt
//
// Closes a notification slot using the alternative callback B (offset +0x18).
// Same pattern as notif_slot_close but dispatches to a different callback.
// Used for cancel/decline actions on yes/no dialogs.
// ---------------------------------------------------------------------------
void notif_slot_close_alt(int param_1)
{
    int slot_idx = *(int*)(param_1 + 0x38);

    // Clear slot state
    g_notification_slots[slot_idx].state = 0;

    // Invoke completion callback B if set
    void (*callback)(void) = g_notification_slots[slot_idx].callback_b;
    if (callback != NULL) {
        callback();
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8004fd9c | Name: notif_slot_close_alt_with_clear
//
// Clears HUD dirty flags on the element, then closes with alt callback.
// Used when a yes/no dialog is cancelled/declined.
// ---------------------------------------------------------------------------
void notif_slot_close_alt_with_clear(int param_1)
{
    hud_screen_clear_dirty_flags(param_1);
    notif_slot_close_alt(param_1);
}

// ---------------------------------------------------------------------------
// Address: 0x8004fdd0 | Name: notif_slot_dismiss
//
// Full dismissal of an active notification slot:
// 1. Sets state to 0 (inactive)
// 2. Applies saved flags (OR mask) back onto the parent HUD element
// 3. Resets the element's transition animation
// 4. Invokes the indexed handler callback (from slot's handler table)
//
// Called by notif_slot_fade_update when fade-out completes.
// Uses FUN_803826f0 to get the current notification context element.
// ---------------------------------------------------------------------------
void notif_slot_dismiss(void)
{
    int element = notif_text_begin(); // FUN_803826f0
    int slot_idx = *(int*)(element + 0x38);
    int slot_offset = slot_idx * 0x224;

    // Clear state
    g_notification_slots[slot_idx].state = 0;

    // Restore saved flags to parent element
    int parent = *(int*)(g_notification_slots_raw[slot_offset + 4] + 0x2C);
    *(uint*)(parent + 0x40) |= g_notification_slots[slot_idx].saved_flags;

    // Reset transition on parent
    hud_element_reset_transition(parent);

    // Get handler index (clamped to >= 0)
    int handler_idx = *(int*)(g_notification_slots_raw[slot_offset + 4] + 0x188);
    if (handler_idx < 0) {
        handler_idx = 0;
    }

    // Invoke indexed callback from handler table (base offset 0x214)
    void (*handler)(int) = g_notification_slots[slot_idx].handlers[handler_idx];
    if (handler != NULL) {
        handler(element);
    }

    notif_text_end(); // FUN_8038273c
}

// ---------------------------------------------------------------------------
// Address: 0x8004fe78 | Name: notif_slot_alloc
//
// Allocates and initializes a notification slot for the current viewport:
// 1. Gets current element context via FUN_803826f0
// 2. Extracts slot index from element +0x38
// 3. Zeroes entire slot (0x224 bytes)
// 4. Copies notification text into slot buffer (max 0x1FF chars)
// 5. Null-terminates the buffer
// 6. Saves element flags (mask 0x20002) for later restoration
//
// Returns pointer to the allocated slot.
// ---------------------------------------------------------------------------
void notif_slot_alloc(void)
{
    int element = notif_text_begin(); // FUN_803826f0
    int slot_idx = *(int*)(element + 0x38);
    int slot_offset = slot_idx * 0x224;

    // Zero the entire slot
    memset_wrapper((int)&g_notification_slots[slot_idx], 0, 0x224);

    // Copy formatted text into slot buffer (0x1FF max)
    notif_text_set_simple(slot_offset + (int)&g_notification_slots_text, element, 0x1FF);

    // Null-terminate
    g_notification_slots[slot_idx].text_terminator = 0;

    // Save element flags for later restore (bits: selectable + input-active)
    g_notification_slots[slot_idx].saved_flags = *(uint*)(element + 0x40) & 0x20002;

    notif_text_end(); // FUN_8038273c
}

// ---------------------------------------------------------------------------
// Address: 0x8004ff00 | Name: notif_show_back_dialog
//
// Shows a notification popup with a single "Back/Dismiss" button.
// If the notification system is inactive (g_notification_active == 0),
// calls the completion callback directly instead of showing UI.
//
// Flow:
// 1. Allocates a notification slot
// 2. Sets state to 1 (active)
// 3. Stores source element and callback
// 4. Disables input flags on the source element (bits 1 and 17)
// 5. Creates a "back" dialog via hud_create_back_dialog
// 6. Stores dialog handle in slot
// ---------------------------------------------------------------------------
void notif_show_back_dialog(void)
{
    int element = notif_begin_with_args(); // FUN_803826ec - gets element + callback in r6

    if (g_notification_active == 0) {
        // System disabled - call callback directly
        callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = callback;

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002; // clear selectable
    *(uint*)(element + 0x40) &= ~0x00020000; // clear input-active

    // Create back dialog with notification text
    slot->hud_handle = hud_create_back_dialog(element, &slot->text_buffer, notif_slot_close_with_clear);

    notif_end_with_args(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x8004ffa0 | Name: notif_show_yes_no_dialog
//
// Shows a notification popup with Yes/No buttons.
// If the notification system is inactive (g_notification_active == 0),
// calls the "yes" callback directly.
//
// Flow:
// 1. Allocates a notification slot
// 2. Sets state to 1, stores source element and both callbacks
// 3. Disables input flags on source element
// 4. Creates yes/no dialog (yes = notif_slot_close_with_clear,
//    no = notif_slot_close_alt_with_clear)
// 5. Sets initial focus to "Yes" option (id=1)
// ---------------------------------------------------------------------------
void notif_show_yes_no_dialog(void)
{
    int element = notif_begin_with_args_2(); // FUN_803826e8 - element + 2 callbacks

    NotificationSlot* slot = notif_slot_alloc();

    if (g_notification_active == 0) {
        // System disabled - call yes callback directly
        yes_callback(element);
        return;
    }

    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = yes_callback;
    slot->callback_b = no_callback;

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    // Create yes/no dialog
    slot->hud_handle = hud_create_yes_no_dialog(
        element,
        &slot->text_buffer,
        notif_slot_close_with_clear,  // Yes action
        notif_slot_close_alt_with_clear  // No action
    );

    // Set initial focus to "Yes"
    if (slot->hud_handle != 0) {
        hud_set_focus_by_id(slot->hud_handle, 1);
    }

    notif_end_with_args_2(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x80050068 | Name: notif_show_yes_no_wrapper
//
// Thin wrapper that calls notif_show_yes_no_dialog.
// Used as an alternate entry point (possibly with different register setup).
// ---------------------------------------------------------------------------
void notif_show_yes_no_wrapper(void)
{
    notif_show_yes_no_dialog();
}

// ---------------------------------------------------------------------------
// Address: 0x80050088 | Name: notif_get_default_string_id
//
// Returns constant 0x196B - the default notification string ID.
// Used as a fallback when no specific notification text is set.
// ---------------------------------------------------------------------------
int notif_get_default_string_id(void)
{
    return 0x196B;
}

// =============================================================================
// Notification Option Dialogs + Fade Timer + Context Accessors (Batch 105)
// Address Range: 0x80050090 - 0x80050704
// Date: 2026-08-10
//
// Completes the notification popup API: option dialogs (2/3-choice), timed
// messages, the per-slot fade timer, and context getter/setter functions.
// =============================================================================

// ---------------------------------------------------------------------------
// Address: 0x80050090 | Name: notif_show_option_dialog
//
// Shows a 2-option notification dialog (OK + default/cancel).
// String 0xe25 = "OK" button label.
// Default string from notif_get_default_string_id (0x196B = "Cancel"/"Back").
// If notification system is inactive, calls the cancel callback directly.
//
// Slot layout:
//   +0x208 (puVar2[0x82]): source element handle
//   +0x214 (puVar2[0x85]): OK callback
//   +0x218 (puVar2[0x86]): Cancel callback
// ---------------------------------------------------------------------------
void notif_show_option_dialog(void)
{
    int element = notif_begin_with_args_2(); // FUN_803826e8

    if (g_notification_active == 0) {
        // System disabled - call cancel callback directly
        cancel_callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = ok_callback;
    slot->callback_b = cancel_callback;

    const char* ok_str = get_string_by_id(0xe25);
    int default_id = notif_get_default_string_id();
    const char* cancel_str = get_string_by_id(default_id);

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    slot->hud_handle = hud_create_option_dialog(/* ok_str, cancel_str */);

    notif_end_with_args_2(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x80050170 | Name: notif_show_option_3_dialog
//
// Shows a 3-option notification dialog (OK + secondary + default/cancel).
// Strings: 0xe25 = "OK", 0xe27 = secondary option, + default cancel.
// If inactive, calls third callback directly.
//
// Slot layout adds:
//   +0x21C (puVar2[0x87]): third callback (cancel/dismiss)
// ---------------------------------------------------------------------------
void notif_show_option_3_dialog(void)
{
    int element = notif_begin_with_args_3(); // FUN_803826e4

    if (g_notification_active == 0) {
        // System disabled - call third callback directly
        third_callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = first_callback;
    slot->callback_b = second_callback;
    slot->callback_c = third_callback;

    get_string_by_id(0xe25);  // "OK"
    get_string_by_id(0xe27);  // Secondary option
    int default_id = notif_get_default_string_id();
    get_string_by_id(default_id);  // Cancel/Back

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    slot->hud_handle = hud_create_option_dialog(/* 3 options */);

    notif_end_with_args_3(); // FUN_80382730
}

// ---------------------------------------------------------------------------
// Address: 0x80050274 | Name: notif_show_option_retry_dialog
//
// Shows a 2-option dialog with "OK" (0xe25) and "Retry" (0x197d).
// If inactive, calls cancel callback directly.
// ---------------------------------------------------------------------------
void notif_show_option_retry_dialog(void)
{
    int element = notif_begin_with_args_2(); // FUN_803826e8

    if (g_notification_active == 0) {
        cancel_callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = ok_callback;
    slot->callback_b = cancel_callback;

    get_string_by_id(0xe25);  // "OK"
    get_string_by_id(0x197d); // "Retry"

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    slot->hud_handle = hud_create_option_dialog(/* ok, retry */);

    notif_end_with_args_2(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x80050354 | Name: notif_show_yes_no_localized_dialog
//
// Shows a 2-option dialog with localized Yes (0x173e) and No (0x173f).
// Used for explicit "Yes"/"No" prompts (vs generic OK/Cancel).
// If inactive, calls cancel callback directly.
// ---------------------------------------------------------------------------
void notif_show_yes_no_localized_dialog(void)
{
    int element = notif_begin_with_args_2(); // FUN_803826e8

    if (g_notification_active == 0) {
        cancel_callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = yes_callback;
    slot->callback_b = no_callback;

    get_string_by_id(0x173e); // "Yes"
    get_string_by_id(0x173f); // "No"

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    slot->hud_handle = hud_create_option_dialog(/* yes, no */);

    notif_end_with_args_2(); // FUN_80382734
}

// ---------------------------------------------------------------------------
// Address: 0x80050434 | Name: notif_show_option_save_dialog
//
// Shows a 3-option dialog for save operations:
// Strings: 0xe25 = "OK", 0x1942 = "Save", 0x1b19 = "Don't Save"
// If inactive, calls cancel callback directly.
// ---------------------------------------------------------------------------
void notif_show_option_save_dialog(void)
{
    int element = notif_begin_with_args_3(); // FUN_803826e4

    if (g_notification_active == 0) {
        cancel_callback();
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 1;
    slot->source_element = element;
    slot->callback_a = first_callback;
    slot->callback_b = second_callback;
    slot->callback_c = third_callback;

    get_string_by_id(0xe25);  // "OK"
    get_string_by_id(0x1942); // "Save"
    get_string_by_id(0x1b19); // "Don't Save"

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    slot->hud_handle = hud_create_option_dialog(/* 3 options */);

    notif_end_with_args_3(); // FUN_80382730
}

// ---------------------------------------------------------------------------
// Address: 0x80050530 | Name: notif_show_timed_message
//
// Shows a timed notification message that auto-dismisses after a duration.
// State is set to 2 (timed/auto-fade) rather than 1 (interactive).
// The duration (float) is passed via FPR and stored in slot +0x20C.
// Uses hud_create_simple_dialog (no buttons, text only).
// If inactive, calls callback directly (if non-null).
// ---------------------------------------------------------------------------
void notif_show_timed_message(void)
{
    int element = notif_begin_with_args(); // FUN_803826ec
    float duration = /* passed via f1 register */;

    if (g_notification_active == 0) {
        if (callback != NULL) {
            callback();
        }
        return;
    }

    NotificationSlot* slot = notif_slot_alloc();
    slot->state = 2; // Timed auto-dismiss
    slot->source_element = element;
    slot->duration = duration;
    slot->callback_a = callback;

    // Disable input on source element
    *(uint*)(element + 0x40) &= ~0x00000002;
    *(uint*)(element + 0x40) &= ~0x00020000;

    // Create simple (no-button) dialog
    slot->hud_handle = hud_create_simple_dialog(element, &slot->text_buffer, 0);

    notif_end_with_args(); // FUN_80382738
}

// ---------------------------------------------------------------------------
// Address: 0x800505e4 | Name: notif_slot_fade_update
//
// Per-slot fade timer update. Called once per frame for each active slot.
// Handles two fade-out states:
//   State 2 (timed message): decrements timer using frame delta time.
//     When timer <= 0: pops HUD screen, resets input timers, calls
//     notif_slot_dismiss (full dismissal with flag restore).
//   State 3 (fade with callback): same fade logic but on completion
//     calls notif_slot_close_with_clear (invokes callback A).
//
// Timer formula: timer -= (fade_speed * (frame_dt / ticks_to_seconds))
// Where fade_speed = FLOAT_806108e8, frame_dt = FLOAT_80613d98
// ---------------------------------------------------------------------------
void notif_slot_fade_update(int slot_idx)
{
    float threshold = FLOAT_80613d9c; // 0.0f (fade complete threshold)
    int offset = slot_idx * 0x224;

    if (g_notification_slots[slot_idx].state == 2) {
        // Timed message: decrement and auto-dismiss
        g_notification_slots[slot_idx].fade_timer -=
            (FADE_SPEED * (FRAME_DT / TICKS_TO_SECONDS));

        if (g_notification_slots[slot_idx].fade_timer <= threshold) {
            hud_screen_pop(g_notification_slots[slot_idx].hud_handle);
            hud_input_reset_repeat_timers();
            notif_slot_dismiss();
        }
    }
    else if (g_notification_slots[slot_idx].state == 3) {
        // Fade with callback: decrement and close-with-clear
        g_notification_slots[slot_idx].fade_timer -=
            (FADE_SPEED * (FRAME_DT / TICKS_TO_SECONDS));

        if (g_notification_slots[slot_idx].fade_timer <= threshold) {
            hud_screen_pop(g_notification_slots[slot_idx].hud_handle);
            hud_input_reset_repeat_timers();
            notif_slot_close_with_clear(g_notification_slots[slot_idx].hud_handle);
        }
    }
}

// ---------------------------------------------------------------------------
// Address: 0x800506f0 | Name: notif_set_event_type
//
// Sets the current notification event type for the next format operation.
// Used before calling notif_format_event_message to select which event
// string template to use.
// ---------------------------------------------------------------------------
void notif_set_event_type(int event_type)
{
    g_notification_event_type = event_type; // DAT_806103f0
}

// ---------------------------------------------------------------------------
// Address: 0x800506f8 | Name: notif_set_player_slot
//
// Sets both the notification player slot index and player data index
// to the same value. Called before formatting notifications that reference
// a specific player (kills, scores, etc.).
// ---------------------------------------------------------------------------
void notif_set_player_slot(int player_slot)
{
    g_notification_player_slot = player_slot; // DAT_80611a90
    g_notification_player_data = player_slot; // DAT_80611a8c
}

// ---------------------------------------------------------------------------
// Address: 0x80050704 | Name: notif_get_player_slot
//
// Returns the current notification player slot index.
// Used by notif_get_controller_mode_name and notif_get_player_name
// to look up the relevant player's data.
// ---------------------------------------------------------------------------
int notif_get_player_slot(void)
{
    return g_notification_player_slot; // DAT_80611a90
}

// =============================================================================
// Notification Context Accessors + Name Lookups (Batch 106, part 1)
// Address Range: 0x8005070c - 0x800507cc
// Date: 2026-08-10
//
// Final notification context getter/setter functions and player name
// resolution helpers. Completes the notification system API surface.
//
// Player name buffers:
//   g_player_name_buffers[8][64]  (DAT_80527b48, stride 0x40) - full names
//   g_player_short_names[8][32]   (DAT_80527bc8, stride 0x20) - abbreviated
// =============================================================================

// ---------------------------------------------------------------------------
// Address: 0x8005070c | Name: notif_get_player_data_alt
//
// Returns the alternate player data index (DAT_80611a8c).
// This is the "other" player reference - e.g., the victim when formatting
// a kill notification for the killer, or vice versa.
// ---------------------------------------------------------------------------
int notif_get_player_data_alt(void)
{
    return g_notification_player_data; // DAT_80611a8c
}

// ---------------------------------------------------------------------------
// Address: 0x80050714 | Name: notif_set_player_data
//
// Sets the player data index (DAT_80611a84).
// This controls which player's data (name, rank, etc.) is used
// by notif_get_player_name and notif_get_player_data.
// ---------------------------------------------------------------------------
void notif_set_player_data(int player_data_idx)
{
    g_notification_player_data_primary = player_data_idx; // DAT_80611a84
}

// ---------------------------------------------------------------------------
// Address: 0x8005071c | Name: notif_get_player_data
//
// Returns the primary player data index (DAT_80611a84).
// Used by notif_get_player_name to look up the name buffer.
// ---------------------------------------------------------------------------
int notif_get_player_data(void)
{
    return g_notification_player_data_primary; // DAT_80611a84
}

// ---------------------------------------------------------------------------
// Address: 0x80050724 | Name: notif_get_name_by_index
//
// Returns a pointer to the full player name (64-char buffer) at the
// given index. Returns empty string if index == -1.
// Buffer: DAT_80527b48 + index * 0x40
// ---------------------------------------------------------------------------
const char* notif_get_name_by_index(int index)
{
    if (index != -1) {
        return &g_player_name_buffers[index]; // stride 0x40
    }
    return g_empty_string; // DAT_806103f4
}

// ---------------------------------------------------------------------------
// Address: 0x80050744 | Name: notif_get_player_name
//
// Returns the current player's full name (64-char) using
// notif_get_player_data() as the index. Returns empty string if -1.
// ---------------------------------------------------------------------------
const char* notif_get_player_name(void)
{
    int idx = notif_get_player_data();
    if (idx == -1) {
        return g_empty_string;
    }
    idx = notif_get_player_data(); // re-read (compiler artifact)
    return &g_player_name_buffers[idx]; // DAT_80527b48 + idx * 0x40
}

// ---------------------------------------------------------------------------
// Address: 0x80050788 | Name: notif_get_player_short_name
//
// Returns the current player's short/abbreviated name (32-char) using
// notif_get_player_data() as the index. Returns empty string if -1.
// Buffer: DAT_80527bc8 + index * 0x20
// ---------------------------------------------------------------------------
const char* notif_get_player_short_name(void)
{
    int idx = notif_get_player_data();
    if (idx == -1) {
        return g_empty_string;
    }
    idx = notif_get_player_data();
    return &g_player_short_names[idx]; // DAT_80527bc8 + idx * 0x20
}

// ---------------------------------------------------------------------------
// Address: 0x800507cc | Name: notif_get_name_by_index_safe
//
// Safe wrapper for notif_get_name_by_index. Redundant -1 check
// (notif_get_name_by_index already handles -1).
// ---------------------------------------------------------------------------
const char* notif_get_name_by_index_safe(int index)
{
    if (index == -1) {
        return g_empty_string;
    }
    return notif_get_name_by_index(index);
}
