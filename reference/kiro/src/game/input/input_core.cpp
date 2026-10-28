// Address: 0x8000c100 - 0x8000c41f | Module: input_core | Date: 2026-08-05
// Input subsystem core functions: controller management, analog processing, rumble
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u32 DAT_804b1130[];   // Controller ID mapping table (4 entries)
extern u32 DAT_804b1144[];   // Controller connected state (stride 0x3c per slot, 8 slots)
extern u32 DAT_80611690;     // Input disabled flag
extern u32 DAT_806116ac;     // Cutscene skip lock flag
extern u32 DAT_806116a4;     // Cutscene state (1=playing, 2=skip requested)
extern f32 FLOAT_806116b0;   // Cutscene skip timer
extern f32 FLOAT_80613670;   // Zero constant / deadzone base
extern f32 FLOAT_80613674;   // Negative analog scale factor
extern f32 FLOAT_80613678;   // Positive analog scale factor
extern f32 FLOAT_8061367c;   // Analog max range
extern f64 DOUBLE_80613680;  // Analog center offset (double)
extern u32 DAT_8060f9e0;     // Active controller count cache

// External function declarations
extern u32 input_get_buttons_pressed(int player);
extern void FUN_803826e0(void);  // OS lock (likely OSDisableInterrupts)
extern void FUN_8038272c(void);  // OS unlock (likely OSRestoreInterrupts)
extern void FUN_8018c8b8(void);  // Rumble setup
extern void FUN_801c047c(s8 intensity, u16 duration);  // Rumble motor command

// 0x8000c100 | input_check_skip_cutscene
// Checks if the player pressed a skip button during a cutscene
void input_check_skip_cutscene(void) {
    u32 buttons = input_get_buttons_pressed(0);

    if (((buttons & 0x81c0) != 0) && (DAT_806116ac == 0)) {
        if ((DAT_806116a4 == 1) || (DAT_806116a4 == 2)) {
            DAT_806116a4 = 2;
            FLOAT_806116b0 = FLOAT_80613670;
        }
    }
}

// 0x8000c15c | input_stub_empty
// Empty stub - possibly removed debug input handler
void input_stub_empty(void) {
    return;
}

// 0x8000c160 | input_find_controller_index
// Find the slot index for a given controller ID. Returns -1 if not found.
int input_find_controller_index(int controller_id) {
    for (int i = 0; i < 4; i++) {
        if (controller_id == (int)DAT_804b1130[i]) {
            return i;
        }
    }
    return -1;
}

// 0x8000c1a0 | input_get_controller_id
// Returns the controller ID mapped to a given slot index
u32 input_get_controller_id(int slot) {
    return DAT_804b1130[slot];
}

// 0x8000c1b4 | input_state_clear
// Clears an input state structure: zeroes digital buttons, sets analogs to center (0x80)
void input_state_clear(int state_ptr) {
    // Clear digital button fields (offsets 0x08 - 0x14)
    *(u32*)(state_ptr + 0x08) = 0;
    *(u32*)(state_ptr + 0x0C) = 0;
    *(u32*)(state_ptr + 0x10) = 0;
    *(u32*)(state_ptr + 0x14) = 0;

    // Set analog axes to center value (0x80 = 128)
    for (int i = 0; i < 7; i++) {
        *(u32*)(state_ptr + 0x18 + i * 4) = 0x80;
    }
}

// 0x8000c1e8 | input_get_active_count
// Returns the number of currently active (connected) controllers
int input_get_active_count(void) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < 8; i++) {
        // Check connected flag at stride 0x3c (60 bytes) per controller slot
        if (DAT_804b1144[i * 0xF] != 0) {
            count++;
        }
    }
    return count;
}

// 0x8000c234 | input_is_connected
// Returns whether a specific controller slot is connected
u32 input_is_connected(int slot) {
    return DAT_804b1144[slot * 0xF];
}

// 0x8000c24c | input_sort_controllers
// Sorts the controller ID table so connected controllers are contiguous
void input_sort_controllers(void) {
    FUN_803826e0();  // Disable interrupts

    int sorted_count = 0;
    int sorted_offset = 0;
    int result = DAT_8060f9e0;

    if (DAT_80611690 == 0) {
        for (int i = 0; i < 4; i++) {
            // Check if slot i is connected (stride 0x3c per slot)
            if (DAT_804b1144[i * 0xF] != 0) {
                if (sorted_count < 4) {
                    u32 old_id = DAT_804b1130[sorted_count];
                    int old_index = input_find_controller_index(i);
                    DAT_804b1130[sorted_count] = (u32)i;
                    DAT_804b1130[old_index] = old_id;
                }
                sorted_count++;
                sorted_offset += 4;
            }
            result = sorted_count;
        }
    }

    DAT_8060f9e0 = result;
    FUN_8038272c();  // Restore interrupts
}

// 0x8000c2fc | input_process_analog
// Processes a raw analog value through deadzone and scaling
// param_1: deadzone threshold, param_2: raw axis value (0-255, center=128)
f64 input_process_analog(f64 deadzone, int raw_value) {
    if (DAT_80611690 != 0) {
        return (f64)FLOAT_80613670;  // Return 0.0 when input disabled
    }

    // Convert raw value to signed float centered at 0
    f32 centered = (f32)((f64)(u32)(raw_value - 0x80) - DOUBLE_80613680);
    if (raw_value < 0x80) {
        centered *= FLOAT_80613674;  // Negative scale
    } else {
        centered /= FLOAT_80613678;  // Positive scale
    }

    f64 result = (f64)centered;

    if (deadzone > (f64)FLOAT_80613670) {
        if (result >= -deadzone) {
            if (result > deadzone) {
                // Above positive deadzone: scale to 0..1 range
                result = (f64)((f32)(result - deadzone) / (f32)((f64)FLOAT_8061367c - deadzone));
            } else {
                // Inside deadzone: return zero
                result = (f64)FLOAT_80613670;
            }
        } else {
            // Below negative deadzone: scale to -1..0 range
            result = (f64)((f32)(result + deadzone) / (f32)((f64)FLOAT_8061367c - deadzone));
        }
    }

    return result;
}

// 0x8000c3cc | input_rumble_if_connected
// Triggers rumble feedback if the specified controller is connected
void input_rumble_if_connected(int rumble_data, int slot) {
    FUN_8018c8b8();  // Rumble setup

    int connected = input_is_connected(slot);
    if (connected != 0) {
        FUN_801c047c((s8)(*(u32*)(rumble_data + 0x10)), (u16)(*(u32*)(rumble_data + 0x0C)));
    }
}
