// autosave_overlay.cpp
// On-screen "saving..." countdown overlay + the rumble-event ring buffer and
// notification-message setter that share its global block. Address range
// 0x801c02c0 - 0x801c04c8 (batch_583).
//
// autosave_countdown_tick / autosave_countdown_render manage a frame countdown
// (DAT_80610cf0). While active, the render pass formats a localized message
// (string id 0x1b40) with the remaining seconds and draws it shadowed near the
// bottom-right of the screen, fading via a sine of the countdown. When the
// countdown expires it pushes a HUD screen (hud_screen_push) after a gate check.
//
// rumble_event_ring_push appends a (masked) rumble event id into a 0x14-slot
// ring buffer (DAT_8044ec88); rumble_event_table_lookup reads a byte from a
// parallel table (DAT_80610cf8). notify_message_set copies a <=0x3f-char
// message into the 0x40-byte notification buffer (DAT_805c2668) and arms a
// 0x78-frame (~2s) display timer (DAT_806125f8).
//
// Non-matching: the 0x43300000 / ^0x80000000 int->double magic-bias idioms are
// reduced to ordinary int<->float conversions; the format/draw call sites keep
// the wide PPC varargs shape via the generated variadic externs.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
extern int    DAT_80610cf0;    // save countdown (frames); <0 = idle
extern int    DAT_806109c0;    // frame-rate denominator (ticks/second)
extern int    DAT_806109c4;    // screen width (pixels)
extern int    DAT_80610cf4;    // format-arg scratch word
extern char   DAT_80610cf8[];  // byte table (rumble/message flags)
extern char   DAT_8044ec88[];  // 0x14-slot rumble-event ring buffer (u16 entries)
extern int    DAT_806125f0;    // rumble ring cursor
extern char   DAT_805c2668[];  // 0x40-byte notification message buffer
extern int    DAT_806125f8;    // notification message display timer (frames)
extern float  FLOAT_80616e80;  // 180-degree scale for the fade sine argument
extern float  FLOAT_80616e84;  // text-width layout scale
extern float  FLOAT_80616e88;  // text baseline Y
extern float  FLOAT_80616e8c;  // fade bias
extern float  FLOAT_80616e90;  // fade amplitude
extern float  FLOAT_80616e94;  // fade normalize divisor
extern double DOUBLE_80616e78; // int->double bias constant
extern double DOUBLE_80616e98; // int->double bias constant (fade)
} // extern "C"

// gate predicate polled when the countdown reaches zero (returns non-zero to
// request the HUD screen push).
static inline unsigned int __ao_should_push_screen(void)
{
    return ((unsigned int (*)(void))0x80366060)();
}
// bounded string copy (strncpy-like): dst, src, maxLen.
static inline void __ao_strncpy(void *dst, int src, int maxLen)
{
    ((void (*)(void *, int, int))0x8038b4cc)(dst, src, maxLen);
}

// ===========================================================================
// Address: 0x801c02c0 | Name: autosave_countdown_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Advance the save-in-progress countdown by one frame. On reaching zero, reset
// to idle (-1) and, if the gate permits, push the follow-up HUD screen.
// ===========================================================================
void autosave_countdown_tick(void)
{
    if (DAT_80610cf0 < 0) return;
    DAT_80610cf0 -= 1;
    if (DAT_80610cf0 == 0) {
        DAT_80610cf0 = -1;
        if (__ao_should_push_screen() != 0) {
            hud_screen_push();
        }
    }
}

// ===========================================================================
// Address: 0x801c0318 | Name: autosave_countdown_render | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Render the "saving..." overlay while the countdown is active: format the
// localized message (string 0x1b40) with the remaining whole seconds, then draw
// it shadowed near the bottom-right, pulsing its alpha via a sine of the
// countdown position within the current second.
// ===========================================================================
void autosave_countdown_render(double param_3, double param_4, double param_5, double param_6,
                               unsigned long long param_7, unsigned long long param_8)
{
    if (DAT_80610cf0 < 0) return;

    unsigned int r7 = 0, r8 = 0, r9 = 0, r10 = 0;
    int fps = DAT_806109c0;
    int countdown = DAT_80610cf0;

    // pulse phase: position within the current second, 0..fps
    int phase = fps - (countdown - (countdown / fps) * fps);
    unsigned int pulse = (unsigned int)((double)FLOAT_80616e80 * (double)(float)phase);

    char msg[104];
    int fmt = get_string_by_id(0x1b40);
    format_string_varargs((unsigned long long)DOUBLE_80616e78, (double)FLOAT_80616e80,
                          param_3, param_4, param_5, param_6, param_7, param_8,
                          (int *)msg, fmt, &DAT_80610cf4, (void *)(intptr_t)(countdown / fps),
                          (void *)(unsigned long long)r7, (void *)(unsigned long long)r8, r9, r10);

    gx_set_sub_material_id(1);
    text_set_font_table(9);
    double width = (double)text_get_char_width((int)msg);

    unsigned int openByte = pulse & 0xff;                 // 0..0xb4 fade index
    unsigned int fadeByte = (0xb4 - pulse) & 0xff;
    int halfW = (int)DAT_806109c4 >> 1;

    // shadowed text alpha ramps with the fade index
    unsigned int alpha = (unsigned int)-(FLOAT_80616e90 *
        ((float)openByte / FLOAT_80616e94) - FLOAT_80616e8c);
    float x = -(float)(width * (double)FLOAT_80616e84 - (double)(float)halfW);

    text_draw_char_shadowed((double)x, (double)FLOAT_80616e88, (int)msg,
                            ((alpha & 0xff) << 8) | 0x4f4f0000 | fadeByte,
                            fadeByte | 0xf0f0f00, r7, r8, r9, r10);
}

// ===========================================================================
// Address: 0x801c047c | Name: rumble_event_ring_push | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Append a rumble event id (masked to 0xeff) into the 0x14-slot ring buffer
// when enabled; advance the cursor with wrap. No-op when disabled (enable==0).
// ===========================================================================
void rumble_event_ring_push(char enable, unsigned short eventId)
{
    if (enable == '\0') return;
    ((unsigned short *)DAT_8044ec88)[DAT_806125f0] = (unsigned short)(eventId & 0xeff);
    DAT_806125f0 = (DAT_806125f0 + 1) % 0x14;
}

// ===========================================================================
// Address: 0x801c04b8 | Name: rumble_event_table_lookup | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Read a signed byte from the rumble/notification flag table.
// ===========================================================================
int rumble_event_table_lookup(int index)
{
    return (int)(char)DAT_80610cf8[index];
}

// ===========================================================================
// Address: 0x801c04c8 | Name: notify_message_set | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Copy a message string (<=0x3f chars) into the notification buffer, null-
// terminate it, and arm the 0x78-frame (~2s) display timer.
// ===========================================================================
void notify_message_set(int srcString)
{
    __ao_strncpy((void *)DAT_805c2668, srcString, 0x3f);
    DAT_805c2668[0x3f] = 0;
    DAT_806125f8 = 0x78;
}

// ===========================================================================
// batch_584 : 0x801c0510 - 0x801c0718
// Notification message renderer + a rumble-sequence cheat-code system built on
// the rumble-event ring buffer (rumble_event_ring_push, batch_583). The cheat
// matcher scans a table of 7 rumble sequences against the ring each frame and,
// on a match, toggles the corresponding cheat flag (DAT_80610cf8[i]), plays an
// on/off click, and posts a notification message.
// ===========================================================================
extern "C" {
extern int  DAT_806125f4;      // "unlock features" cheat toggle flag
// Cheat-code table (7 entries, 0x30 stride) laid out across three adjacent
// arrays: DAT_8044ecc4 = per-entry u16 rumble sequences, DAT_8044ecec = per-
// entry sequence length, PTR_FUN_8044ecf0 = per-entry match callback.
extern short DAT_8044ecc4[];   // packed rumble sequences (u16 each)
extern int   DAT_8044ecec[];   // per-entry sequence length
extern void (*PTR_FUN_8044ecf0[])(void); // per-entry match callbacks
extern char  s_UNLOCK_FEATURES___d_8044ecb0[]; // "UNLOCK FEATURES : %d"
extern float FLOAT_80616ea0;   // notification message screen position (x=y)
} // extern "C"

// debug_log_formatted is declared void in generated_decls, but the ROM returns
// the formatted-string pointer (fed straight into notify_message_set). Route
// through a typed pointer to recover the return value.
typedef int (*DebugLogFmtFn)(unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, unsigned long long,
                             unsigned long long, unsigned long long, const char *fmt,
                             unsigned int, unsigned int, unsigned int, unsigned int,
                             unsigned int, unsigned int, unsigned int);
static const DebugLogFmtFn __ao_debug_log_formatted = (DebugLogFmtFn)0x8012e878;
// debug-log accumulator/context getter used to seed debug_log_formatted's acc.
static inline unsigned long long __ao_debug_log_ctx(void)
{
    return ((unsigned long long (*)(void))0x801c0a88)();
}

// ===========================================================================
// Address: 0x801c0510 | Name: notify_message_render | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Render the notification message set by notify_message_set while its display
// timer (DAT_806125f8) is positive; decrement the timer each frame. Drawn with
// a drop shadow at a fixed screen position (font table 3).
// ===========================================================================
void notify_message_render(void)
{
    if (DAT_806125f8 <= 0) return;
    DAT_806125f8 -= 1;
    gx_set_sub_material_id(1);
    text_set_font_table(3);
    text_draw_char_drop_shadow((double)FLOAT_80616ea0, (double)FLOAT_80616ea0,
                               (int)(intptr_t)DAT_805c2668, 0x7f7f7f7f, 0x7f);
}

// ===========================================================================
// Address: 0x801c0570 | Name: cheat_flag_is_zero | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Branchless (value == 0) predicate via count-leading-zeros >> 5.
// ===========================================================================
unsigned int cheat_flag_is_zero(int value)
{
    return countLeadingZeros((unsigned int)value) >> 5;
}

// ===========================================================================
// Address: 0x801c057c | Name: cheat_toggle_flag | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Toggle the cheat flag at DAT_80610cf8[index] (only when index != 0), then
// play the corresponding on (0xd11) / off (0xd13) UI click sound.
// ===========================================================================
void cheat_toggle_flag(int index)
{
    if (cheat_flag_is_zero(index) != 0) return;   // guard: index==0 -> CLZ>>5==1
    DAT_80610cf8[index] = (char)(1 - DAT_80610cf8[index]);
    int params[12];
    sound_init_play_params((int)params);
    params[0] = 0xd13;                             // off click
    if (DAT_80610cf8[index] != '\0') {
        params[0] = 0xd11;                         // on click
    }
    sound_play_request(params);
}

// ===========================================================================
// Address: 0x801c05f0 | Name: cheat_toggle_unlock_features | Date: 2026-09-07
// ---------------------------------------------------------------------------
// "UNLOCK FEATURES" cheat: toggle the DAT_806125f4 flag, play the on/off click,
// format the "UNLOCK FEATURES : %d" status string and post it as a notification.
// ===========================================================================
void cheat_toggle_unlock_features(unsigned long long param_1, unsigned long long param_2,
                                  unsigned long long param_3, unsigned long long param_4,
                                  unsigned long long param_5, unsigned long long param_6,
                                  unsigned long long param_7, unsigned long long param_8,
                                  unsigned int param_9, unsigned int param_10, unsigned int param_11,
                                  unsigned int param_12, unsigned int param_13, unsigned int param_14,
                                  unsigned int param_15, unsigned int param_16)
{
    (void)param_1; (void)param_9; (void)param_10;
    DAT_806125f4 = 1 - DAT_806125f4;
    int params[14];
    sound_init_play_params((int)params);
    params[0] = 0xd13;
    if (DAT_806125f4 != 0) {
        params[0] = 0xd11;
    }
    sound_play_request(params);

    int msg = __ao_debug_log_formatted(__ao_debug_log_ctx(), param_2, param_3, param_4, param_5,
                                       param_6, param_7, param_8, s_UNLOCK_FEATURES___d_8044ecb0,
                                       (unsigned int)DAT_806125f4, param_11, param_12, param_13,
                                       param_14, param_15, param_16);
    notify_message_set(msg);
}

// ===========================================================================
// Address: 0x801c0660 | Name: cheat_toggle_slot1 | Date: 2026-09-07
// Cheat-table callback: toggle flag slot 1.
// ===========================================================================
void cheat_toggle_slot1(void) { cheat_toggle_flag(1); }

// ===========================================================================
// Address: 0x801c0684 | Name: cheat_toggle_slot2 | Date: 2026-09-07
// Cheat-table callback: toggle flag slot 2.
// ===========================================================================
void cheat_toggle_slot2(void) { cheat_toggle_flag(2); }

// ===========================================================================
// Address: 0x801c06a8 | Name: cheat_toggle_slot3 | Date: 2026-09-07
// Cheat-table callback: toggle flag slot 3.
// ===========================================================================
void cheat_toggle_slot3(void) { cheat_toggle_flag(3); }

// ===========================================================================
// Address: 0x801c06cc | Name: cheat_toggle_slot4 | Date: 2026-09-07
// Cheat-table callback: toggle flag slot 4.
// ===========================================================================
void cheat_toggle_slot4(void) { cheat_toggle_flag(4); }

// ===========================================================================
// Address: 0x801c06f0 | Name: cheat_toggle_slot5 | Date: 2026-09-07
// Cheat-table callback: toggle flag slot 5.
// ===========================================================================
void cheat_toggle_slot5(void) { cheat_toggle_flag(5); }

// ===========================================================================
// Address: 0x801c0718 | Name: cheat_code_match_tick | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Per-frame cheat-code matcher. For each of the 7 cheat entries (0x30 stride),
// compare its rumble sequence (DAT_8044ecc4, length DAT_8044ecec) against the
// most-recent entries in the rumble-event ring buffer (DAT_8044ec88, walked
// backward from the cursor DAT_806125f0). On a full match, invoke the entry's
// callback (PTR_FUN_8044ecf0) and clear the whole ring so the code can't
// re-fire until re-entered.
// ===========================================================================
void cheat_code_match_tick(void)
{
    for (unsigned int entry = 0; entry < 7; ++entry) {
        int stride = entry * 0x30;
        int seqLen = *(int *)((int)DAT_8044ecec + stride);
        // Walk the ring backward: start (cursor + 0x14 - seqLen) frames back.
        int ringIdx = (DAT_806125f0 + 0x14) - seqLen;
        int seqOff = 0;
        bool matched = true;
        for (int i = 0; i < seqLen && matched; ++i) {
            if (((short *)DAT_8044ec88)[ringIdx % 0x14] !=
                *(short *)((int)DAT_8044ecc4 + seqOff + stride)) {
                matched = false;
            }
            ringIdx = ringIdx % 0x14 + 1;
            seqOff += 2;
        }
        if (matched) {
            (*(void (**)(void))((int)PTR_FUN_8044ecf0 + stride))();
            for (int i = 0; i < 0x14; ++i) {
                ((short *)DAT_8044ec88)[i] = 0;
            }
        }
    }
}
