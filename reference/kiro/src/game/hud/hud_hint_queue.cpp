// Batch 345 | Date: 2026-09-04
// HUD hint / command-record queue. The HUD keeps a small array of "command"
// records (8 x 0x1c bytes) used to script tutorial prompts / objective hints. A
// record is: [+0x00 id][+0x04..0x0c three arg slots][+0x18 arg-list count]. This
// group is the low-level accessor + reset/set/append API used by the many
// hint-builder routines (FUN_8012b3xx .. FUN_8012d5xx).
//
//   0x8012b20c hud_hint_get_command_field  (read a command field, or default table)
//   0x8012b274 hud_hint_record_reset       (id = -1, count = 0)
//   0x8012b288 hud_hint_record_reset_args  (reset + clear the 3 arg slots)
//   0x8012b2c8 hud_hint_record_set3        (set a 3-arg command record)
//   0x8012b2d8 hud_hint_record_append_arg  (append one arg, bump count)
//   0x8012b2f4 hud_hint_record_set_arg0    (set first arg, reset iterator)
//   0x8012b304 hud_hint_system_reset       (reset all 8 records + globals)
//   0x8012b36c hud_hint_system_clear       (clear state fields + reset system)
//
// Subsystem: game (HUD)
//
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615730;   // 0.0
extern int    DAT_80611d78;     // player-slot table base (per-slot command block)
extern int    DAT_803cca34;     // default command field table (8 x 2-word entries, via &NAME)

extern int    FUN_803826f0(void);   // HUD push (hint system), returns state block
extern void   FUN_8038273c(void);   // HUD pop  (hint system)

// forward decls (this file)
extern "C" {
int  hud_hint_get_command_field(int field, int slot);
void hud_hint_record_reset(int* rec);
void hud_hint_record_reset_args(int* rec);
void hud_hint_record_set3(int* rec, int a, int b, int c);
void hud_hint_record_append_arg(int rec, int arg);
void hud_hint_record_set_arg0(int rec, int arg);
void hud_hint_system_reset(void);
void hud_hint_system_clear(int state);
}

// ============================================================================
// 0x8012b20c | hud_hint_get_command_field
// Reads command field 'field' for player 'slot'. Fields < 0xe come from the
// per-slot command block (DAT_80611d78 + slot*0x141c + 0x18); higher indices are
// served from a small default table (DAT_803cca34, copied to a local scratch).
// ============================================================================
extern "C" int hud_hint_get_command_field(int field, int slot) {
    int value;
    if (field < 0xe) {
        value = *(int*)(DAT_80611d78 + slot * 0x141c + field * 4 + 0x18);
    } else {
        int scratch[14];
        // copy the 7 two-word default entries into the local scratch, then index
        int* src = &DAT_803cca34;
        int* dst = scratch;
        for (int i = 0; i < 7; i++) {
            dst[1] = src[1];
            dst[2] = src[2];
            src += 2;
            dst += 2;
        }
        value = scratch[field];
    }
    return value;
}

// ============================================================================
// 0x8012b274 | hud_hint_record_reset
// Reset a command record header: mark id (-1) invalid and clear the arg count.
// ============================================================================
extern "C" void hud_hint_record_reset(int* rec) {
    rec[6] = 0;      // +0x18 arg count
    rec[0] = -1;     // +0x00 id
    return;
}

// ============================================================================
// 0x8012b288 | hud_hint_record_reset_args
// Reset the record header, then invalidate the three inline arg slots.
// ============================================================================
extern "C" void hud_hint_record_reset_args(int* rec) {
    hud_hint_record_reset(rec);
    for (int i = 0; i < 3; i++) {
        rec[3] = -1;   // +0x0c (original clears the same slot 3x; preserved)
    }
    return;
}

// ============================================================================
// 0x8012b2c8 | hud_hint_record_set3
// Store a 3-argument command record (id + two args in slots 0..2).
// ============================================================================
extern "C" void hud_hint_record_set3(int* rec, int a, int b, int c) {
    rec[0] = a;
    rec[1] = b;
    rec[2] = c;
    return;
}

// ============================================================================
// 0x8012b2d8 | hud_hint_record_append_arg
// Append one arg to a record's variable arg list (count at +0x18, args from
// +0x0c), bumping the count.
// ============================================================================
extern "C" void hud_hint_record_append_arg(int rec, int arg) {
    int n = *(int*)(rec + 0x18);
    *(int*)(rec + 0x18) = n + 1;
    *(int*)(rec + n * 4 + 0xc) = arg;
    return;
}

// ============================================================================
// 0x8012b2f4 | hud_hint_record_set_arg0
// Set the first arg (+0x0c) and reset the arg iterator (+0x18 = -1).
// ============================================================================
extern "C" void hud_hint_record_set_arg0(int rec, int arg) {
    *(int*)(rec + 0xc) = arg;
    *(int*)(rec + 0x18) = -1;
    return;
}

// ============================================================================
// 0x8012b304 | hud_hint_system_reset
// Reset the hint system: clear the accumulated offset vector (+0xfc..0x104),
// the pending flag (+0xf8) and head index (+0xe0), then reset all 8 command
// records (0x1c stride).
// ============================================================================
extern "C" void hud_hint_system_reset(void) {
    int state = FUN_803826f0();
    float zero = FLOAT_80615730;
    *(float*)(state + 0xfc) = FLOAT_80615730;
    *(float*)(state + 0x100) = zero;
    *(float*)(state + 0x104) = zero;
    *(int*)(state + 0xf8) = 0;
    *(int*)(state + 0xe0) = 0;
    int off = 0;
    for (int i = 0; i < 8; i++) {
        hud_hint_record_reset_args((int*)(state + off));
        off += 0x1c;
    }
    FUN_8038273c();
    return;
}

// ============================================================================
// 0x8012b36c | hud_hint_system_clear
// Clear the extra per-view hint state fields (+0xe4..0xf4) and reset the system.
// ============================================================================
extern "C" void hud_hint_system_clear(int state) {
    *(int*)(state + 0xe4) = 0;
    *(int*)(state + 0xe8) = 0;
    *(int*)(state + 0xec) = 0;
    *(int*)(state + 0xf0) = 0;
    *(int*)(state + 0xf4) = 0;
    hud_hint_system_reset();
    return;
}
