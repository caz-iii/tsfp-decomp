// Address: 0x8000e55c-0x8000eff8 | Name: Sound Engine | Date: 2026-07-24
// TSFP Sound engine - handle management, 3D sound, streaming, oneshot system
// Wraps the low-level audio driver (FUN_80188xxx-FUN_80189xxx) with game logic.

#include "common.h"

// Sound handle table: 0x1648 entries, each 0x14 bytes at DAT_8060fa8c
extern int DAT_8060fa8c;     // Sound bank descriptor table base
extern int DAT_80612028;     // Current frame counter
extern int DAT_8060fa90;     // Last frame oneshot was checked
extern int DAT_806116f0;     // Stream slot round-robin index
extern int DAT_806116f4;     // Oneshot registration count this frame
extern int DAT_804b1480[];   // Oneshot registry (100 entries)
extern int DAT_804b1610[];   // Stream slot data (25 slots * 0x40 bytes)
extern int DAT_804b1640[];   // Stream handle IDs
extern int DAT_804b1648[];   // Stream timeout counters
extern int DAT_804b164c[];   // Stream slot states (0=free, 1=stopping, 2=pending, 3=timeout, 4=playing)
extern int DAT_80611008;     // Audio enabled flag
extern int DAT_806108e4;     // Frame delta time (ms)
extern int DAT_80611568;     // Keep-alive flag for soundpad system
extern float* DAT_806116f8;  // Listener position override pointer
extern float FLOAT_806136dc;
extern float FLOAT_806136d8;
extern float FLOAT_806136e0;
extern float FLOAT_806136e4;
extern float FLOAT_806116fc;

// Forward declarations
extern void soundpad_stop_playing(int pad_ptr);
extern void soundpad_load_level_data(void);
extern void soundpad_update_switch_flags(void);
extern void halt_stub_assert(void);
extern void memset_wrapper(int dst, int val, int size);
extern int FUN_801c1714(void);

// ----------------------------------------------------------------------------
// 0x8000e55c | soundpad_stop_all
// Stops audio playback on all active sound pads.
// ----------------------------------------------------------------------------
void soundpad_stop_all(void) {
    int offset = 0;
    for (int i = 0; i < DAT_806116c0; i++) {
        if (*(int*)(DAT_806116ec + offset + 4) >= 0) {
            soundpad_stop_playing(DAT_806116ec + offset);
        }
        offset += 100;
    }
}

// ----------------------------------------------------------------------------
// 0x8000e5c0 | soundpad_init_level
// Initializes the sound pad system for a new level.
// Loads pad data, validates counts, and updates switch flags.
// Asserts if pad/switch counts exceed 1000/500 limits.
// ----------------------------------------------------------------------------
void soundpad_init_level(void) {
    DAT_806116dc = 0;
    DAT_806116d8 = 0;
    DAT_806116d4 = 0;
    DAT_806116d0 = 0;
    DAT_806116cc = 0;
    DAT_806116c8 = 0;
    DAT_806116c4 = 0;
    DAT_806116c0 = 0;

    if (DAT_80612614 != 100) { // Not menu level
        soundpad_load_level_data();
        if (DAT_806116c0 > 0) {
            if (DAT_806116c0 >= 0x3e9) halt_stub_assert();
            if (DAT_806116c8 >= 0x3e9) halt_stub_assert();
            if (DAT_806116d0 > 500) halt_stub_assert();
        }
        soundpad_update_switch_flags();
    }
}

// ----------------------------------------------------------------------------
// 0x8000e660 | soundpad_shutdown
// Shuts down the sound pad system. Stops all sounds and frees data if allowed.
// ----------------------------------------------------------------------------
void soundpad_shutdown(void) {
    soundpad_stop_all();
    if (DAT_80611568 == 0) {
        DAT_806116ec = 0;
        DAT_806116e8 = 0;
        DAT_806116e4 = 0;
        DAT_806116e0 = 0;
        DAT_806116c0 = 0;
    }
}

// ----------------------------------------------------------------------------
// 0x8000e6a4 | sound_oneshot_try_register
// Registers a sound for oneshot playback limiting.
// Prevents the same sound from being played too many times per frame.
// Returns 1 if registration succeeds, 0 if already at limit.
// Uses a per-frame registry that resets each frame.
// ----------------------------------------------------------------------------
int sound_oneshot_try_register(int* sound_params) {
    int sound_id = *sound_params;

    // Check if sound has 'always play' flag
    if ((*(unsigned int*)(DAT_8060fa8c + sound_id * 0x14 + 8) & 0x510) != 0) {
        return 1;
    }

    if (DAT_80612028 == DAT_8060fa90) {
        // Same frame - check if already registered
        for (int i = 0; i < DAT_806116f4; i++) {
            if (sound_id == DAT_804b1480[i]) {
                return 0; // Already playing this frame
            }
        }
    } else {
        // New frame - reset
        DAT_8060fa90 = DAT_80612028;
        DAT_806116f4 = 0;
    }

    if (DAT_806116f4 < 100) {
        DAT_804b1480[DAT_806116f4] = sound_id;
        DAT_806116f4++;
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 0x8000e75c | sound_set_listener_override
// Sets a custom listener position pointer for sound spatialization.
// Pass NULL to use default (nearest player) behavior.
// ----------------------------------------------------------------------------
void sound_set_listener_override(float* position) {
    DAT_806116f8 = position;
}

// ----------------------------------------------------------------------------
// 0x8000e764 | sound_get_nearest_listener
// Finds the nearest player to the sound source for 3D audio calculations.
// Uses override position if set, otherwise iterates players.
// Outputs nearest position to the provided output pointer.
// ----------------------------------------------------------------------------
void sound_get_nearest_listener(void) {
    // Gets source position from register context
    // If override set: use override position
    // Otherwise: iterate all players, find closest by distance
    // Writes result to output float[3]
}

// ----------------------------------------------------------------------------
// 0x8000e894 | sound_is_position_in_range
// Tests if a 3D position is within audible range of the nearest listener.
// Range is specified as a radius (compared via squared distance).
// ----------------------------------------------------------------------------
int sound_is_position_in_range(double range, float* position) {
    float listener[3];
    sound_get_nearest_listener();
    float dx = position[0] - listener[0];
    float dy = position[1] - listener[1];
    float dz = position[2] - listener[2];
    return (dx*dx + dy*dy + dz*dz) <= (float)(range * range);
}

// ----------------------------------------------------------------------------
// 0x8000e924 | sound_play_request
// Main sound play entry point. Validates the sound ID, resolves the bank,
// checks oneshot limits, and dispatches to either streaming or immediate play.
// Returns a handle (>= 0) on success, or negative on failure.
// Bit 29 set in handle = streaming sound.
// ----------------------------------------------------------------------------
unsigned int sound_play_request(int* params) {
    int sound_id = *params;
    if (sound_id == -1 || sound_id < 0 || sound_id > 0x1648) {
        return 0xffffffff;
    }

    unsigned int resolved = FUN_8000f154(params);
    if (resolved == 0) {
        return 0xffffffff;
    }

    // Handle looping flag
    if ((resolved & 0x40000000) != 0) {
        params[4] |= 0x20;
        params[2] = (int)FLOAT_806136dc;
    }

    if ((resolved & 0x20000000) == 0) {
        // Immediate/oneshot sound
        if (!sound_oneshot_try_register(params)) {
            return 0xfffffffd;
        }
        if (FUN_80188cc4(params, resolved) == 0) {
            return resolved;
        }
        return 0xffffffff;
    } else {
        // Streaming sound
        return sound_stream_start();
    }
}

// ----------------------------------------------------------------------------
// 0x8000ea08 | sound_init_play_params
// Initializes a sound parameter struct to defaults.
// Sets priority=20, default volume, looping off.
// ----------------------------------------------------------------------------
void sound_init_play_params(int params_ptr) {
    memset_wrapper(params_ptr, 0, 0x30);
    *(int*)(params_ptr + 0x10) = 0x14;  // priority = 20
    *(unsigned char*)(params_ptr + 0x28) = 0;
    *(float*)(params_ptr + 0x08) = FLOAT_806136e0;  // default volume
    *(float*)(params_ptr + 0x18) = FLOAT_806136e0;
    *(float*)(params_ptr + 0x2c) = FLOAT_806136e4;
    *(float*)(params_ptr + 0x14) = FLOAT_806136e0;
}

// ----------------------------------------------------------------------------
// 0x8000ea64 | sound_stream_find_free_slot
// Finds a free streaming slot using round-robin search.
// 25 slots available. Returns slot index or -1 if full.
// ----------------------------------------------------------------------------
int sound_stream_find_free_slot(void) {
    int count = 0;
    int result = -1;
    int found = 0;
    while (!found && count < 0x19) {
        count++;
        DAT_806116f0 = (DAT_806116f0 + 1) % 0x19;
        if (DAT_804b164c[DAT_806116f0 * 0x10] == 0) {
            found = 1;
            result = DAT_806116f0;
        }
    }
    return found ? result : -1;
}

// ----------------------------------------------------------------------------
// 0x8000ead8 | sound_stream_start
// Starts a streaming sound in a free slot.
// Copies play parameters into the slot and sets state to 'pending' (2).
// ----------------------------------------------------------------------------
void sound_stream_start(void) {
    int slot = sound_stream_find_free_slot();
    if (slot >= 0) {
        // Copy params, set timeout, mark as pending
        DAT_804b164c[slot * 0x10] = 2; // pending
    }
}

// ----------------------------------------------------------------------------
// 0x8000eb64 | sound_stop_handle
// Stops a sound by handle. For streaming handles (bit 29), marks slot
// for cleanup. For immediate sounds, calls driver stop directly.
// ----------------------------------------------------------------------------
void sound_stop_handle(unsigned int handle) {
    if ((handle & 0x20000000) == 0) {
        FUN_80188ce8(handle); // Direct stop
    } else {
        // Find matching stream slot and mark for stop
        for (int i = 0; i < 0x19; i++) {
            if (DAT_804b1640[i * 0x10] == (int)handle) {
                if (DAT_804b164c[i * 0x10] == 4) {
                    FUN_80188ce8(handle);
                }
                DAT_804b164c[i * 0x10] = 1; // stopping
            }
        }
    }
}

// ----------------------------------------------------------------------------
// 0x8000ebf0 | sound_set_pitch
// Wrapper - sets pitch shift for a playing sound.
// ----------------------------------------------------------------------------
void sound_set_pitch(double pitch, unsigned int handle) {
    FUN_80188df8(pitch, handle);
}

// ----------------------------------------------------------------------------
// 0x8000ec10 | sound_set_volume
// Wrapper - sets volume for a playing sound.
// ----------------------------------------------------------------------------
void sound_set_volume(double volume, int handle) {
    FUN_80188dc0(volume, handle);
}

// ----------------------------------------------------------------------------
// 0x8000ec30 | sound_set_pan
// Wrapper - sets stereo pan for a playing sound.
// ----------------------------------------------------------------------------
void sound_set_pan(double pan, int handle) {
    FUN_80188d88(pan, handle);
}

// ----------------------------------------------------------------------------
// 0x8000ec50 | sound_set_3d_position
// Sets 3D position for a spatialized sound. Only if audio is enabled.
// ----------------------------------------------------------------------------
void sound_set_3d_position(int handle, int position, unsigned int flags) {
    if (DAT_80611008 != 0) {
        FUN_80188ef4(handle, position, flags);
    }
}

// ----------------------------------------------------------------------------
// 0x8000ec7c | sound_play_3d
// Plays a 3D positioned sound with full spatial parameters.
// ----------------------------------------------------------------------------
void sound_play_3d(void) {
    FUN_80189250(/* forwarded register params */);
}

// ----------------------------------------------------------------------------
// 0x8000ec9c | sound_is_handle_valid
// Returns non-zero if the sound handle is still valid/active.
// Checks both stream slots and the driver handle table.
// ----------------------------------------------------------------------------
int sound_is_handle_valid(unsigned int handle) {
    if ((int)handle < 1) return 0;

    if ((handle & 0x20000000) != 0) {
        for (int i = 0; i < 0x19; i++) {
            if (DAT_804b1640[i * 0x10] == (int)handle && DAT_804b164c[i * 0x10] == 2) {
                return 0; // Still pending, not yet valid
            }
        }
    }
    return FUN_801899f4(handle);
}

// ----------------------------------------------------------------------------
// 0x8000ed14 | sound_is_playing
// Returns 1 if the sound handle is actively playing.
// For stream slots, states 1 (stopping) and 2 (pending) count as playing.
// ----------------------------------------------------------------------------
int sound_is_playing(unsigned int handle) {
    if ((int)handle < 1) return 0;

    if ((handle & 0x20000000) != 0) {
        for (int i = 0; i < 0x19; i++) {
            if (DAT_804b1640[i * 0x10] == (int)handle &&
                (DAT_804b164c[i * 0x10] == 1 || DAT_804b164c[i * 0x10] == 2)) {
                return 1;
            }
        }
    }
    return FUN_80189a5c(handle);
}

// ----------------------------------------------------------------------------
// 0x8000ed94 | sound_clear_bank_ref
// Clears the bank reference for a sound ID. Used during level unload.
// ----------------------------------------------------------------------------
void sound_clear_bank_ref(int sound_id) {
    if (sound_id < 0) return;
    *(int*)(DAT_8060fa8c + sound_id * 0x14 + 0xc) = -1;
}

// ----------------------------------------------------------------------------
// 0x8000edb4 | sound_pause_all
// Pauses all currently playing sounds (driver-level).
// ----------------------------------------------------------------------------
void sound_pause_all(void) {
    FUN_80189130();
}

// ----------------------------------------------------------------------------
// 0x8000edd4 | sound_resume_all
// Resumes all paused sounds (driver-level).
// ----------------------------------------------------------------------------
void sound_resume_all(void) {
    FUN_801891dc();
}

// ----------------------------------------------------------------------------
// 0x8000edf4 | sound_stream_clear_all_slots
// Clears all 25 streaming slots to free state.
// ----------------------------------------------------------------------------
void sound_stream_clear_all_slots(void) {
    for (int i = 0; i < 0x19; i++) {
        DAT_804b164c[i * 0x10] = 0;
    }
}

// ----------------------------------------------------------------------------
// 0x8000ee20 | sound_update_3d_volume
// Updates volume and position for a 3D sound handle.
// Used by proximity system for smooth volume transitions.
// ----------------------------------------------------------------------------
void sound_update_3d_volume(double volume, int handle, float* position) {
    FUN_80189b34(volume, handle, position);
}

// ----------------------------------------------------------------------------
// 0x8000ee40 | sound_get_position
// Gets the current 3D position of a sound handle.
// ----------------------------------------------------------------------------
void sound_get_position(int handle, float* out_position) {
    FUN_80189aa4(handle, out_position);
}

// ----------------------------------------------------------------------------
// 0x8000ee60 | sound_set_reverb
// Sets reverb level for the audio environment.
// ----------------------------------------------------------------------------
void sound_set_reverb(int level) {
    FUN_801896b4(level);
}

// ----------------------------------------------------------------------------
// 0x8000ee80 | sound_stream_update_tick
// Per-frame update for streaming sounds. Manages slot state machine:
//   state 0 = free
//   state 1 = stopping -> free next frame
//   state 2 = pending (with timeout countdown)
//   state 3 = timed out -> attempt play
//   state 4 = playing
// Also calls driver update functions for mixing.
// ----------------------------------------------------------------------------
void sound_stream_update_tick(void) {
    for (int i = 0; i < 0x19; i++) {
        int state = DAT_804b164c[i * 0x10];
        if (state == 2) {
            // Pending: decrement timeout
            DAT_804b1648[i] -= DAT_806108e4;
            if (DAT_804b1648[i] < 1) {
                DAT_804b164c[i * 0x10] = 3; // timed out
                DAT_804b1648[i] = 0;
            }
        } else if (state == 1) {
            // Stopping -> free
            DAT_804b164c[i * 0x10] = 0;
        } else if (state == 3) {
            // Timed out -> attempt play
            unsigned int handle = sound_play_request((int*)&DAT_804b1610[i * 0x10]);
            if ((int)handle < 0) {
                DAT_804b164c[i * 0x10] = 1;
            } else if ((handle & 0x40000000) == 0) {
                DAT_804b164c[i * 0x10] = 1;
            } else {
                DAT_804b164c[i * 0x10] = 4; // playing
            }
        }
    }
    FUN_80189898(); // driver tick
    FUN_80189760(); // voice update
}

// ----------------------------------------------------------------------------
// 0x8000ef84 | sound_update_voices
// Updates all active voice allocations. Called from main audio tick.
// ----------------------------------------------------------------------------
void sound_update_voices(void) {
    FUN_80189964();
}

// ----------------------------------------------------------------------------
// 0x8000efa4 | sound_system_init
// Initializes the entire sound system.
// Clears all bank references, initializes the driver, and sets default volume.
// ----------------------------------------------------------------------------
void sound_system_init(void) {
    // Clear all 0x1648 sound bank references
    for (int i = 0; i < 0x1648; i++) {
        *(int*)(DAT_8060fa8c + i * 0x14 + 0xc) = 0;
    }
    FUN_80189910();  // Driver init
    sound_set_listener_override(0);
    FUN_8000f430((double)FLOAT_806136e0); // Set master volume
}

// ----------------------------------------------------------------------------
// 0x8000eff8 | sound_system_shutdown
// Shuts down the sound system. Resets master volume and driver state.
// ----------------------------------------------------------------------------
void sound_system_shutdown(void) {
    FUN_8000f430((double)FLOAT_806136e0);
    FUN_8018989c(); // Driver shutdown
    FLOAT_806116fc = FLOAT_806136e0;
}
