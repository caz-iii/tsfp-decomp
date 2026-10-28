// Address: 0x8000c420 - 0x8000c758 | Module: input_update | Date: 2026-08-05
// Input subsystem: polling, button state queries, update loop
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u32 DAT_804b1130[];    // Controller ID mapping (4 slots)
extern u32 DAT_804b114c[];    // Current button state (stride 0x3c per pad)
extern u32 DAT_804b1150[];    // Previous frame button state
extern u32 DAT_804b1154[];    // Released buttons (replay mode)
extern u32 DAT_804b1140[];    // Rumble data per controller (stride 0x3c)
extern u32 DAT_804b1178[];    // Previous frame held (for pressed calc)
extern u32 DAT_804b123c[];    // Replay mode current buttons
extern u32 DAT_804b1240[];    // Replay mode held buttons
extern u32 DAT_804b1244[];    // Replay mode released buttons
extern u32 DAT_804b1264[];    // Replay source buffer
extern u32 DAT_804b1410[];    // Button hold timer (4 pads)
extern u32 DAT_804b1420[];    // Previous button state copy
extern u32 DAT_804b1430[];    // Button repeat cooldown timers
extern u32 DAT_80611690;      // Input disabled flag
extern u32 DAT_80611698;      // Input update semaphore
extern u32 DAT_80612614;      // Game state / level mode
extern u32 DAT_80612e70;      // Replay mode active flag
extern u32 DAT_806108e0;      // Frame delta time (int)
extern f32 FLOAT_80613688;    // Analog activity lower threshold (negative)
extern f32 FLOAT_8061368c;    // Analog activity upper threshold (positive)

// External function declarations
extern void FUN_803826e0(void);     // OSDisableInterrupts
extern void FUN_8038272c(void);     // OSRestoreInterrupts
extern void FUN_8018ca88(void);     // PAD polling
extern int  FUN_801c1714(void);     // Get replay controller count
extern void FUN_801c0718(void);     // Replay state update
extern void input_check_skip_cutscene(void);
extern void input_rumble_if_connected(int rumble_data, int slot);
extern u32  input_get_buttons_pressed(int player);
extern f64  input_get_analog_value(int player, int axis);

// 0x8000c420 | input_any_activity
// Returns 1 if any button is pressed or any analog axis exceeds its activity threshold
u32 input_any_activity(int player) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    u32 buttons = input_get_buttons_pressed(player);
    if (buttons != 0) {
        return 1;
    }

    f64 lower = (f64)FLOAT_80613688;
    f64 upper = (f64)FLOAT_8061368c;

    for (int axis = 0; axis < 7; axis++) {
        f64 val = input_get_analog_value(player, axis);
        if (val < lower) {
            return 1;
        }
        val = input_get_analog_value(player, axis);
        if (val > upper) {
            return 1;
        }
    }

    return 0;
}

// 0x8000c4e4 | input_update_all
// Main per-frame input update: polls pads, processes rumble, updates button history
void input_update_all(void) {
    FUN_803826e0();  // Disable interrupts
    DAT_80611698 = 0;
    FUN_8018ca88();  // Poll hardware PADs

    // In replay mode, copy replay data into live state
    if ((DAT_80611690 == 0) && (DAT_80612e70 != 0)) {
        int offset_id = 0;
        int offset_state = 0;
        int count = FUN_801c1714();
        for (int i = 0; i < count; i++) {
            int* p_id = (int*)((int)DAT_804b1130 + offset_id);
            u32* p_replay = (u32*)((int)DAT_804b1264 + offset_state);
            offset_id += 4;
            offset_state += 0x3c;
            DAT_804b114c[*p_id * 0xF + 2] = *p_replay;
        }
    }

    // Per-controller: clear previous held state, trigger rumble
    for (int i = 0; i < 4; i++) {
        DAT_804b1178[i * 0xF] = 0;
        input_rumble_if_connected((int)DAT_804b1140 + i * 0x3c, i);
    }

    // Copy current buttons to hold timers and manage repeat cooldowns
    int frame_dt = DAT_806108e0;
    for (int i = 0; i < 4; i++) {
        u32 prev = DAT_804b1150[i * 0xF];
        DAT_804b1410[i] = DAT_804b114c[i * 0xF];
        DAT_804b1420[i] = prev;
        if ((int)DAT_804b1430[i] > 0) {
            u32 remaining = DAT_804b1430[i] - frame_dt;
            DAT_804b1430[i] = remaining & ~((int)remaining >> 31);  // clamp to 0
        }
    }

    // In replay mode, write live state back to replay buffer
    if ((DAT_80611690 == 0) && (DAT_80612e70 != 0)) {
        int offset_id = 0;
        int offset_state = 0;
        int count = FUN_801c1714();
        for (int i = 0; i < count; i++) {
            int* p_id = (int*)((int)DAT_804b1130 + offset_id);
            offset_id += 4;
            DAT_804b1264[offset_state / 4] = DAT_804b114c[*p_id * 0xF + 2];
            offset_state += 0x3c;
        }
    }

    FUN_801c0718();  // Replay state update

    // Cutscene skip check (only during gameplay)
    if (DAT_80612614 < 0x65) {
        input_check_skip_cutscene();
    }

    FUN_8038272c();  // Restore interrupts
}

// 0x8000c690 | input_get_buttons_pressed
// Returns buttons that are newly pressed this frame (current & ~previous)
u32 input_get_buttons_pressed(int player) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    if (DAT_80612e70 != 0) {
        // Replay mode: use replay buffer
        return DAT_804b123c[player * 0xF] & ~DAT_804b1264[player * 0xF + 0xB];
    }

    // Normal mode: map through controller ID table
    u32 slot = DAT_804b1130[player];
    return DAT_804b114c[slot * 0xF] & ~DAT_804b1178[slot * 0xF];
}

// 0x8000c6fc | input_get_buttons_held
// Returns buttons currently held down
u32 input_get_buttons_held(int player) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    if (DAT_80612e70 != 0) {
        // Replay mode
        return DAT_804b1240[player * 0xF];
    }

    // Normal mode
    u32 slot = DAT_804b1130[player];
    return DAT_804b1150[slot * 0xF];
}

// 0x8000c758 | input_get_buttons_released
// Returns buttons released this frame
u32 input_get_buttons_released(int player) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    if (DAT_80612e70 != 0) {
        // Replay mode
        return DAT_804b1244[player * 0xF];
    }

    // Normal mode
    u32 slot = DAT_804b1130[player];
    return DAT_804b1154[slot * 0xF];
}
