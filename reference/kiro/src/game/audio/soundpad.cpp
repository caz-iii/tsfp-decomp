// Address: 0x8000ce2c-0x8000dfdc | Name: Sound Pad System | Date: 2026-07-24
// TSFP Sound pad subsystem - proximity-based ambient audio triggers for levels
// Parses "soundpaddata" files, manages sound/switch pad states, triggers audio
// based on player proximity and switch-to-sound link relationships.

#include "common.h"

// Sound pad globals
extern int DAT_806116b8;     // Current overlay flags (buttons being shown)
extern int DAT_806116c0;     // Number of sound pads
extern int DAT_806116c4;     // numSoundPads backup
extern int DAT_806116c8;     // Number of switch pads
extern int DAT_806116cc;     // numSwitchPads backup
extern int DAT_806116d0;     // Number of switch-to-sound links
extern int DAT_806116d4;     // numSwitch2SoundLinks backup
extern int DAT_806116d8;     // Number of switch-to-switch links
extern int DAT_806116dc;     // numSwitch2SwitchLinks backup
extern int DAT_806116e0;     // Switch-to-switch link array pointer
extern int DAT_806116e4;     // Switch-to-sound link array pointer
extern int DAT_806116e8;     // Switch pad array pointer (0x2C bytes per entry)
extern int DAT_806116ec;     // Sound pad array pointer (100 bytes per entry)

extern int DAT_805015c0;     // Game state flags
extern int DAT_80612614;     // Current level ID
extern int DAT_80612610;     // Number of players
extern int DAT_80611d78;     // Player data base pointer
extern int DAT_80611b14;     // Max floor ID

extern float FLOAT_80613698;
extern float FLOAT_8061369c;
extern float FLOAT_806136a0;
extern float FLOAT_806136a8;
extern float FLOAT_806136b0;
extern float FLOAT_806136b8;
extern float FLOAT_806136bc;
extern float FLOAT_806136c0;
extern float FLOAT_806136c4;
extern float FLOAT_806136c8;
extern float FLOAT_806136cc;
extern float FLOAT_806136d0;

extern float DAT_80583228[];  // Sin/Cos lookup table

// Forward declarations
void soundpad_activate_switch(void);
void soundpad_deactivate_switch(void);
void soundpad_stop_playing(int pad_ptr);
void soundpad_play_or_update(double volume, int pad_ptr);
double soundpad_distance_squared_xz(float* a, float* b);
int soundpad_find_pad_by_id(int id);
int soundpad_find_switch_by_id(int id);
int soundpad_check_floor_id(int floor_id);

// ----------------------------------------------------------------------------
// 0x8000ce2c | input_can_show_overlay
// Returns 1 if the button overlay HUD can be displayed.
// Blocked during cutscenes and specific game states.
// ----------------------------------------------------------------------------
int input_can_show_overlay(void) {
    if ((DAT_805015c0 & 0xe0000200) != 0) {
        return 0;
    }
    if ((DAT_80612614 < 0x65) && (DAT_80611d78 != 0) &&
        (*(int*)(DAT_80611d78 + 0x114) == 0x100)) {
        return 0;
    }
    return 1;
}

// ----------------------------------------------------------------------------
// 0x8000d00c | soundpad_stub_nop
// Empty stub - placeholder for unimplemented sound pad function.
// ----------------------------------------------------------------------------
void soundpad_stub_nop(void) {
    return;
}

// ----------------------------------------------------------------------------
// 0x8000d010 | soundpad_update_switch_flags
// Updates switch pad activation flags based on the switch-to-sound link table.
// Clears 'active' bit on all pads, then re-sets it for pads with active links.
// ----------------------------------------------------------------------------
void soundpad_update_switch_flags(void) {
    // Clear bit 1 (active) on all sound pads
    int offset = 0;
    for (int i = 0; i < DAT_806116c0; i++) {
        *(unsigned int*)(DAT_806116ec + offset + 0x60) &= ~2u;
        offset += 100;
    }

    // Set bit 1 on sound pads referenced by active switch-to-sound links
    int link_offset = 0;
    for (int i = 0; i < DAT_806116d0; i++) {
        if ((*(unsigned int*)(DAT_806116e4 + link_offset + 4) & 8) == 0) {
            int pad_offset = *(short*)(DAT_806116e4 + link_offset + 2) * 100 + 0x60;
            *(unsigned int*)(DAT_806116ec + pad_offset) |= 2u;
        }
        link_offset += 8;
    }
}

// ----------------------------------------------------------------------------
// 0x8000d098 | soundpad_find_switch_by_id
// Binary search for a switch pad by its external reference ID.
// Returns index or -1 if not found.
// ----------------------------------------------------------------------------
int soundpad_find_switch_by_id(int target_id) {
    int lo = 0;
    int hi = DAT_806116c8;
    int last = -1;

    do {
        int old_hi = hi;
        int mid = lo + (old_hi - lo >> 1);
        if (mid == last) {
            return -1;
        }
        int entry_id = *(int*)(DAT_806116e8 + mid * 0x2c);
        last = mid;
        if (target_id < entry_id) {
            hi = mid;
        } else if (entry_id < target_id) {
            lo = mid;
            hi = old_hi;
        } else {
            return mid;
        }
    } while (1);
}

// ----------------------------------------------------------------------------
// 0x8000d0f4 | soundpad_find_pad_by_id
// Binary search for a sound pad by its external reference ID.
// Returns index or -1 if not found.
// ----------------------------------------------------------------------------
int soundpad_find_pad_by_id(int target_id) {
    int lo = 0;
    int hi = DAT_806116c0;
    int last = -1;

    do {
        int old_hi = hi;
        int mid = lo + (old_hi - lo >> 1);
        if (mid == last) {
            return -1;
        }
        int entry_id = *(int*)(DAT_806116ec + mid * 100);
        last = mid;
        if (target_id < entry_id) {
            hi = mid;
        } else if (entry_id < target_id) {
            lo = mid;
            hi = old_hi;
        } else {
            return mid;
        }
    } while (1);
}

// ----------------------------------------------------------------------------
// 0x8000d150 | soundpad_distance_squared_xz
// Returns squared distance on the XZ plane between two 3D positions.
// Used for proximity checks (avoids sqrt).
// ----------------------------------------------------------------------------
double soundpad_distance_squared_xz(float* a, float* b) {
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];
    return (double)(dx * dx + dz * dz);
}

// ----------------------------------------------------------------------------
// 0x8000d174 | soundpad_get_data_path
// Constructs the file path for the level's sound pad data file.
// Returns pointer to static path buffer at 0x804b1440.
// Format: "host0/pad/soundpaddata_level_d_s"
// ----------------------------------------------------------------------------
void* soundpad_get_data_path(void) {
    FUN_80387fb8(/* params */, "host0/pad/soundpaddata_level_d_s", DAT_80612614);
    return &DAT_804b1440;
}

// ----------------------------------------------------------------------------
// 0x8000d1b4 | soundpad_load_level_data
// Loads and parses the sound pad data file for the current level.
// Allocates memory for sound pads, switch pads, and link tables.
// Parses header, soundPadBegin/End, switchPadBegin/End, link sections.
// Each sound pad entry is 100 bytes; each switch pad is 0x2C bytes.
// ----------------------------------------------------------------------------
void soundpad_load_level_data(void) {
    // [Large function - parses soundpaddata file]
    // Key parsed fields per sound pad:
    //   extref, id, sound, pitch, position (xyz), floor, radius, falloff, minvol, flags
    // Key parsed fields per switch pad:
    //   extref, id, position (xyz), floor, radius
    // Link sections: switch2Sound and switch2Switch references
    // See full decompilation in Ghidra for complete implementation
}

// ----------------------------------------------------------------------------
// 0x8000dc20 | soundpad_find_pad_by_extref
// Linear search for a sound pad by external reference ID.
// Returns pointer to pad entry or 0 if not found.
// ----------------------------------------------------------------------------
int soundpad_find_pad_by_extref(int extref) {
    int offset = 0;
    int count = DAT_806116c0;
    if (count > 0) {
        do {
            if (extref == *(int*)(DAT_806116ec + offset)) {
                return DAT_806116ec + offset;
            }
            offset += 100;
            count--;
        } while (count != 0);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 0x8000dc5c | soundpad_play_or_update
// Starts or updates audio playback for a sound pad.
// If no sound handle exists (-1), creates a new one with position/pitch/volume.
// Otherwise updates the existing sound's volume based on proximity.
// param_1: volume factor (0.0 to 1.0)
// param_2: pointer to sound pad entry
// ----------------------------------------------------------------------------
void soundpad_play_or_update(double volume, int pad_ptr) {
    // Calculate volume from pad's min/max range
    unsigned int vol_min = *(unsigned char*)(pad_ptr + 0x5c);
    unsigned int vol_range = *(unsigned char*)(pad_ptr + 0x5d) - vol_min;

    float base_vol = FLOAT_806136a0 * (float)vol_min;
    float final_vol = FLOAT_806136a0 * (float)((float)volume * (float)vol_range) + base_vol;

    if (*(int*)(pad_ptr + 4) == -1) {
        // No active sound - create new
        // Constructs sound params struct and calls FUN_8000e924 (sound_create)
        // Sets pad_ptr+4 to the sound handle
    } else {
        // Update existing sound volume/position
        FUN_8000ee20(final_vol, *(int*)(pad_ptr + 4), (void*)(pad_ptr + 0x1c));
    }
}

// ----------------------------------------------------------------------------
// 0x8000dd7c | soundpad_stop_playing
// Stops audio for a sound pad and invalidates its handle.
// ----------------------------------------------------------------------------
void soundpad_stop_playing(int pad_ptr) {
    if (*(int*)(pad_ptr + 4) >= 0) {
        FUN_8000eb64(); // sound_stop
        *(int*)(pad_ptr + 4) = -1;
    }
}

// ----------------------------------------------------------------------------
// 0x8000ddbc | soundpad_activate_switch
// Recursively activates a switch pad and propagates through switch-to-switch links.
// Sets the activation bit for the given switch and follows linked switches.
// ----------------------------------------------------------------------------
void soundpad_activate_switch(void) {
    int switch_index = /* from register context */;
    int bit_index = /* from register context */;

    int switch_ptr = DAT_806116e8 + switch_index * 0x2c;
    unsigned int bit = 1u << bit_index;

    // Set activation bit
    *(unsigned int*)(switch_ptr + 0x24) |= bit;

    // Propagate through switch-to-switch links
    int link_count = DAT_806116d8;
    int link_offset = link_count * 8;
    while (link_count > 0) {
        link_offset -= 8;
        link_count--;
        short* link = (short*)(DAT_806116e0 + link_offset);
        if (link[0] == switch_index &&
            (bit & *(unsigned int*)(DAT_806116e8 + link[1] * 0x2c + 0x24)) == 0) {
            soundpad_activate_switch(); // recursive
        } else if (link[1] == switch_index &&
                   (bit & *(unsigned int*)(DAT_806116e8 + link[0] * 0x2c + 0x24)) == 0) {
            soundpad_activate_switch(); // recursive
        }
    }
}

// ----------------------------------------------------------------------------
// 0x8000de98 | soundpad_deactivate_switch
// Recursively deactivates a switch pad and propagates through links.
// Clears the activation bit and follows linked switches.
// ----------------------------------------------------------------------------
void soundpad_deactivate_switch(void) {
    int switch_index = /* from register context */;
    int bit_index = /* from register context */;

    int switch_ptr = DAT_806116e8 + switch_index * 0x2c;
    unsigned int bit = 1u << bit_index;

    // Clear activation bit
    *(unsigned int*)(switch_ptr + 0x24) &= ~bit;

    // Propagate through switch-to-switch links
    int link_count = DAT_806116d8;
    int link_offset = link_count * 8;
    while (link_count > 0) {
        link_offset -= 8;
        link_count--;
        short* link = (short*)(DAT_806116e0 + link_offset);
        if (link[0] == switch_index &&
            (bit & *(unsigned int*)(DAT_806116e8 + link[1] * 0x2c + 0x24)) != 0) {
            soundpad_deactivate_switch(); // recursive
        } else if (link[1] == switch_index &&
                   (bit & *(unsigned int*)(DAT_806116e8 + link[0] * 0x2c + 0x24)) != 0) {
            soundpad_deactivate_switch(); // recursive
        }
    }
}

// ----------------------------------------------------------------------------
// 0x8000df74 | soundpad_check_floor_id
// Checks if the given floor ID is active/valid for sound pad proximity.
// Returns 1 if valid, 0 if out of range.
// ----------------------------------------------------------------------------
int soundpad_check_floor_id(int floor_id) {
    if (floor_id < 1 || floor_id > DAT_80611b14) {
        return 0;
    }
    // Additional floor validation logic
    return 1;
}

// ----------------------------------------------------------------------------
// 0x8000dfdc | soundpad_update_proximity
// Main sound pad tick function. For each active sound pad:
//   - Checks if any player is within range (XZ distance + Y bounds)
//   - For switch pads: checks if player is in trigger volume (sphere or box)
//   - Activates/deactivates switches based on player proximity
//   - Starts/stops/updates sound playback based on proximity volume falloff
// Supports both spherical (radius) and oriented box trigger volumes.
// Uses sin/cos LUT at DAT_80583228 for rotated box collision.
// ----------------------------------------------------------------------------
void soundpad_update_proximity(void) {
    // [Large function - main proximity update loop]
    // Iterates all switch pads, checks player proximity
    // For each player in range: activate/deactivate switch
    // Then iterates all sound pads:
    //   - If pad is active and has active switch link: compute volume by distance
    //   - Supports sphere and OBB (oriented bounding box) trigger shapes
    //   - Calls soundpad_play_or_update() with computed volume
    //   - Calls soundpad_stop_playing() when player exits range
    // See full decompilation in Ghidra at 0x8000dfdc
}
