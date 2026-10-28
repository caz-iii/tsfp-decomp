// Address: 0x8000c7b4-0x8000cc70 | Name: Input System | Date: 2026-07-24
// TSFP Input subsystem - pad state management, button/analog queries, rumble

#include "common.h"

// External data
extern int DAT_80612e70;
extern int DAT_80611690;
extern int DAT_80611934;
extern int DAT_804b1130[];   // Pad mapping table (player -> physical pad index)
extern int DAT_804b1140[];   // Physical pad state array (4 pads, 0xf dwords each = 0x3C bytes)
extern int DAT_804b1230[];   // Extended pad state array (8 virtual pads)

extern float FLOAT_80613670;
extern float FLOAT_80613694;

// Forward declarations
void input_clear_buttons(int pad_index, unsigned int button_mask);
float input_process_analog(double value, int deadzone);
void input_get_buttons_pressed(int pad_index);

// ----------------------------------------------------------------------------
// 0x8000c7b4 | input_clear_buttons
// Clears specified button bits from the pad's pressed state and sets them
// in the released mask.
// ----------------------------------------------------------------------------
void input_clear_buttons(int pad_index, unsigned int button_mask) {
    unsigned int* pad_state;

    if (DAT_80612e70 == 0) {
        pad_state = (unsigned int*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
    } else {
        pad_state = (unsigned int*)(&DAT_804b1230[pad_index * 0xf]);
    }

    pad_state[0xe] = pad_state[0xe] | button_mask;
    pad_state[4] = pad_state[4] & ~button_mask;
}

// ----------------------------------------------------------------------------
// 0x8000c80c | input_reset_pad
// Resets a single pad - clears all buttons and sets analog axes to neutral (0x80).
// ----------------------------------------------------------------------------
void input_reset_pad(int pad_index) {
    unsigned int* pad_state;

    if (DAT_80612e70 == 0) {
        pad_state = (unsigned int*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
    } else {
        pad_state = (unsigned int*)(&DAT_804b1230[pad_index * 0xf]);
    }

    input_clear_buttons(pad_index, 0xfff);

    int offset = 0;
    int count = 7;
    do {
        *(unsigned int*)((char*)pad_state + offset + 0x18) = 0x80;
        offset += 4;
        count--;
    } while (count != 0);
}

// ----------------------------------------------------------------------------
// 0x8000c894 | input_reset_all_pads
// Resets all 4 physical pads to neutral state.
// ----------------------------------------------------------------------------
void input_reset_all_pads(void) {
    int pad_offset = 0;
    int pad_index = 0;

    do {
        // Set released mask to all buttons
        *(unsigned int*)((char*)&DAT_804b1140 + pad_offset + 0x38) |= 0xfff;
        // Clear pressed mask
        *(unsigned int*)((char*)&DAT_804b1140 + pad_offset + 0x10) &= 0xfffff000;

        // Reset analog axes to neutral (0x80)
        int axis_count = 7;
        int axis_offset = 0;
        do {
            *(unsigned int*)((char*)&DAT_804b1140 + pad_offset + 0x18 + axis_offset) = 0x80;
            axis_count--;
            axis_offset += 4;
        } while (axis_count != 0);

        pad_index++;
        pad_offset += 0x3c;
    } while (pad_index < 4);
}

// ----------------------------------------------------------------------------
// 0x8000c8f4 | input_set_analog_value
// Sets a raw analog value on the specified pad. Blocked if input is locked.
// ----------------------------------------------------------------------------
void input_set_analog_value(double value, int pad_index) {
    if (DAT_80611690 != 0) {
        return;
    }

    float* pad_state;
    if (DAT_80612e70 == 0) {
        pad_state = (float*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
    } else {
        pad_state = (float*)(&DAT_804b1230[pad_index * 0xf]);
    }

    *pad_state = (float)value;
}

// ----------------------------------------------------------------------------
// 0x8000c944 | input_get_analog_value
// Returns a processed analog axis value for the given pad and axis.
// Returns zero if input is locked.
// ----------------------------------------------------------------------------
double input_get_analog_value(int pad_index, int axis_index) {
    if (DAT_80611690 == 0) {
        float* pad_state;
        if (DAT_80612e70 == 0) {
            pad_state = (float*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
        } else {
            pad_state = (float*)(&DAT_804b1230[pad_index * 0xf]);
        }
        return input_process_analog((double)*pad_state, (int)pad_state[axis_index + 6]);
    }
    return (double)FLOAT_80613670;
}

// ----------------------------------------------------------------------------
// 0x8000c9c4 | input_poll_debug
// Debug polling - reads buttons pressed (called in debug mode only).
// ----------------------------------------------------------------------------
void input_poll_debug(void) {
    // Unreachable blocks removed by Ghidra
    int pad_index = FUN_803826f0(); // OS context get
    if (DAT_80611690 == 0) {
        input_get_buttons_pressed(pad_index);
        input_get_buttons_pressed(pad_index);
    }
    FUN_8038273c(); // OS context restore
}

// ----------------------------------------------------------------------------
// 0x8000ca60 | input_poll_and_process
// Main input polling entry point - reads pad, processes analog, then debug poll.
// ----------------------------------------------------------------------------
void input_poll_and_process(void) {
    // Unreachable blocks removed by Ghidra
    int pad_index = FUN_803826f0();
    input_get_analog_value(pad_index, 0);
    input_poll_debug();
    FUN_8038273c();
}

// ----------------------------------------------------------------------------
// 0x8000cafc | input_set_rumble_intensity
// Sets rumble intensity for a pad. Clamped to 0-127. Only active in mode 7.
// ----------------------------------------------------------------------------
void input_set_rumble_intensity(int pad_index, int intensity) {
    if (DAT_80611690 != 0) {
        return;
    }

    if (DAT_80611934 != 7) {
        intensity = 0;
    }

    unsigned int* pad_state;
    if (DAT_80612e70 == 0) {
        pad_state = (unsigned int*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
    } else {
        pad_state = (unsigned int*)(&DAT_804b1230[pad_index * 0xf]);
    }

    int clamped = 0x7f;
    if (intensity < 0x80) {
        clamped = intensity;
    }
    pad_state[0xd] = clamped;
}

// ----------------------------------------------------------------------------
// 0x8000cb6c | input_get_rumble_intensity
// Returns the current rumble intensity for a pad. 0 if input locked.
// ----------------------------------------------------------------------------
unsigned int input_get_rumble_intensity(int pad_index) {
    if (DAT_80611690 != 0) {
        return 0;
    }

    unsigned int* pad_state;
    if (DAT_80612e70 == 0) {
        pad_state = (unsigned int*)(&DAT_804b1140[DAT_804b1130[pad_index] * 0xf]);
    } else {
        pad_state = (unsigned int*)(&DAT_804b1230[pad_index * 0xf]);
    }

    return pad_state[0xd];
}

// ----------------------------------------------------------------------------
// 0x8000cbc4 | input_get_buttons_held
// Returns the button held bitfield for a pad. 0 if input locked.
// ----------------------------------------------------------------------------
unsigned int input_get_buttons_held(int pad_index) {
    if (DAT_80611690 != 0) {
        return 0;
    }
    return *(unsigned int*)(&DAT_804b1140 + pad_index * 0xf + 0x10);
}

// ----------------------------------------------------------------------------
// 0x8000cbf0 | input_system_init
// Initializes the input system. Zeros out all pad state arrays.
// Called early in game startup.
// ----------------------------------------------------------------------------
void input_system_init(/* many params from register spill */) {
    FUN_8018c360(/* forwarded params */);

    float initial = FLOAT_80613694;
    int offset = 0;
    int count = 4;
    do {
        *(float*)((char*)&DAT_804b1140 + offset) = initial;
        offset += 0x3c;
        count--;
    } while (count != 0);

    offset = 0;
    count = 8;
    do {
        *(float*)((char*)&DAT_804b1230 + offset) = initial;
        offset += 0x3c;
        count--;
    } while (count != 0);
}

// ----------------------------------------------------------------------------
// 0x8000cc58 | input_set_pad_mapping
// Sets the physical-to-logical pad mapping for a player slot.
// ----------------------------------------------------------------------------
void input_set_pad_mapping(int player_index, int physical_pad) {
    DAT_804b1130[player_index] = physical_pad;
}

// ----------------------------------------------------------------------------
// 0x8000cc6c | input_stub_nop
// Empty stub - unused input function.
// ----------------------------------------------------------------------------
void input_stub_nop(void) {
    return;
}

// ----------------------------------------------------------------------------
// 0x8000cc70 | input_get_button_name
// Returns the localized string name for a button bitmask.
// Uses a switch-style lookup with string table references.
// ----------------------------------------------------------------------------
void input_get_button_name(int button_mask, int* out_string) {
    switch (button_mask) {
        case 0x001: // A
        case 0x002: // B
            FUN_8038b510(out_string, &DAT_8060f9e4);
            break;
        case 0x003: // X
        case 0x004: // Y
            FUN_8038b510(out_string, &DAT_8060f9e8);
            break;
        case 0x008: // Z
            FUN_8038b510(out_string, &DAT_8060f9ec);
            break;
        case 0x00c: // L
            FUN_8038b510(out_string, &DAT_8060f9f0);
            break;
        case 0x010: // R
            FUN_8038b510(out_string, &DAT_8060f9f4);
            break;
        case 0x020: // DPad
            FUN_8038b510(out_string, &DAT_8060f9f8);
            break;
        case 0x040:
            FUN_8038b510(out_string, &DAT_8060f9fc);
            break;
        case 0x080:
            FUN_8038b510(out_string, &DAT_8060fa00);
            break;
        case 0x100: // START
            FUN_8038b510(out_string, (int*)s_START_8060fa04);
            break;
        case 0x200:
            FUN_8038b510(out_string, (int*)get_string_by_id(0x18fc));
            break;
        case 0x400:
            FUN_8038b510(out_string, (int*)get_string_by_id(0x1905));
            break;
        case 0x800:
            FUN_8038b510(out_string, (int*)get_string_by_id(0x190e));
            break;
        default:
            FUN_8038b510(out_string, &DAT_8060fa0c);
            break;
    }
}
