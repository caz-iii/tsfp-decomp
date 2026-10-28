// Address: 0x8000f028-0x8000faa0 | Name: Music System | Date: 2026-07-24
// TSFP Music subsystem - track loading, playback, volume, pause/resume
// Manages streaming music through 3 channels with level-based track selection.

#include "common.h"

// Music globals
extern float FLOAT_8060fa94;  // Master volume
extern int DAT_8060fa98;      // Current music track index
extern unsigned int DAT_8060faa0; // Channel mute bitmask
extern unsigned int DAT_8060fa88; // Handle counter (auto-increment)
extern int DAT_8060fa8c;      // Sound bank descriptor table
extern int DAT_80611004;      // Audio subsystem A enabled
extern int DAT_80611008;      // Audio subsystem B enabled
extern int DAT_8061100c;      // Music volume setting (user preference)
extern int DAT_80611010;      // SFX volume setting
extern int DAT_80611700;      // Music volume applied
extern int DAT_80611704;      // Music paused flag
extern int DAT_80612614;      // Current level ID
extern int DAT_80612ab0;      // Current playing track ID
extern float FLOAT_806116fc;  // Music fade volume
extern float FLOAT_80613708;
extern float FLOAT_8061370c;
extern float FLOAT_80613710;
extern float FLOAT_80613714;
extern float FLOAT_80613720;
extern float FLOAT_80613724;
extern float FLOAT_80613728;

// Forward declarations
extern void sound_stream_clear_all_slots(void);

// ----------------------------------------------------------------------------
// 0x8000f028 | sound_reset_streams
// Resets all streaming audio slots and the low-level stream driver.
// Called during level transitions.
// ----------------------------------------------------------------------------
void sound_reset_streams(void) {
    sound_stream_clear_all_slots();
    FUN_80189be4();
}

// ----------------------------------------------------------------------------
// 0x8000f04c | sound_get_bank_flags
// Returns bank-specific flags for a sound ID to determine playback behavior.
// Handles level-specific sounds, cutscene audio, and common banks.
// Returns: 0=skip, 4=normal, 0x20=cutscene, 4=special level override
// ----------------------------------------------------------------------------
int sound_get_bank_flags(int sound_id) {
    unsigned int flags = *(unsigned int*)(DAT_8060fa8c + sound_id * 0x14 + 8);

    if (DAT_80612614 == 0x66 || DAT_80612614 == 0x65) {
        // Menu/credits levels
        if ((flags & 4) != 0 && ((0x80 - (DAT_80612614 == 0x66)) == (flags >> 24))) {
            return 4;
        }
    } else {
        if ((flags & 0x20) == 0) {
            // Normal level
            if ((flags & 4) != 0 && DAT_80612614 != (int)(flags >> 24)) {
                return 0; // Wrong level
            }
            if ((flags & 3) != 0 && *(int*)(DAT_8060fa8c + sound_id * 0x14 + 0xc) != -1) {
                return 0; // Bank not loaded
            }
            return 4;
        }
        // Cutscene sound
        if (DAT_805015bd == '\n' || (DAT_805015c0 & 0x40) != 0) {
            return 0x20;
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 0x8000f154 | sound_resolve_handle_flags
// Generates a unique handle with encoded behavior flags.
// Bits encode: streaming(29), looping(30), reverb(28), priority(25), spatial(26)
// Auto-increments global handle counter (wraps at 0x3ffffff).
// ----------------------------------------------------------------------------
unsigned int sound_resolve_handle_flags(int* params) {
    unsigned int handle = DAT_8060fa88++;
    if (DAT_8060fa88 > 0x3ffffff) {
        DAT_8060fa88 = 1;
    }

    unsigned int bank_flags = *(unsigned int*)(DAT_8060fa8c + *params * 0x14 + 8);
    if (bank_flags & 0x1000) handle |= 0x2000000;   // Priority flag
    if (bank_flags & 0x0040) handle |= 0x10000000;  // Reverb flag
    if (bank_flags & 0x0400) handle |= 0x4000000;   // Spatial flag
    if (bank_flags & 0x0010) handle |= 0x40000000;  // Looping flag
    if (params[1] != 0)      handle |= 0x20000000;  // Streaming flag

    return handle;
}

// ----------------------------------------------------------------------------
// 0x8000f1d4 | sound_compute_3d_volume
// Computes 3D spatialized volume based on listener-source distance.
// Applies distance attenuation curve and outputs final volume level.
// Used by the proximity sound system and positional SFX.
// ----------------------------------------------------------------------------
void sound_compute_3d_volume(/* params */) {
    // Computes distance from source to nearest listener
    // Applies linear distance attenuation
    // Outputs clamped 14-bit volume value (0-0x3FFF)
}

// ----------------------------------------------------------------------------
// 0x8000f428 | sound_get_master_volume
// Returns the current master volume setting (0.0 - 1.0).
// ----------------------------------------------------------------------------
float sound_get_master_volume(void) {
    return FLOAT_8060fa94;
}

// ----------------------------------------------------------------------------
// 0x8000f430 | sound_set_master_volume
// Sets the master volume. Affects all subsequent volume calculations.
// ----------------------------------------------------------------------------
void sound_set_master_volume(double volume) {
    FLOAT_8060fa94 = (float)volume;
}

// ----------------------------------------------------------------------------
// 0x8000f438 | sound_fade_master_volume
// Initiates a smooth fade to a target master volume over time.
// ----------------------------------------------------------------------------
void sound_fade_master_volume(double target) {
    FUN_8012edfc(target, (double)FLOAT_80613708, &FLOAT_806116fc);
}

// ----------------------------------------------------------------------------
// 0x8000f460 | sound_compute_effective_volume
// Computes effective playback volume considering hardware volume,
// master volume, and music fade. Used to scale individual sounds.
// param_1: 0=apply music fade, other=raw volume
// ----------------------------------------------------------------------------
double sound_compute_effective_volume(int apply_fade) {
    float hw_vol = 0.0f;
    if (DAT_80611008 != 0) {
        hw_vol = (float)((DAT_80611010 - 0x80000000u) * FLOAT_8061370c);
    }
    double vol = (double)(hw_vol * sound_get_master_volume());
    if (apply_fade == 0) {
        vol = (double)((float)vol * FLOAT_806116fc);
    }
    return vol;
}

// ----------------------------------------------------------------------------
// 0x8000f4f0 | music_load_bank
// Loads a music sample bank from disk into memory.
// Wrapper around low-level bank loader.
// ----------------------------------------------------------------------------
void music_load_bank(void) {
    FUN_8018b6c8(/* forwarded params */);
}

// ----------------------------------------------------------------------------
// 0x8000f510 | music_load_level_bank
// Loads the music bank for the current level ("pak/musicts.pak").
// Called during level load to prepare streaming music data.
// ----------------------------------------------------------------------------
void music_load_level_bank(void) {
    void* pak = FUN_8002a66c("pak/musicts.pak", 0, 1);
    void* bank = FUN_8018b9d4(pak);
    FUN_8000fc6c(bank);
}

// ----------------------------------------------------------------------------
// 0x8000f548 | music_load_track
// Loads a specific music track into the streaming buffer.
// ----------------------------------------------------------------------------
void music_load_track(char* track_name) {
    FUN_8018b9b0(track_name);
}

// ----------------------------------------------------------------------------
// 0x8000f568 | music_queue_track
// Queues a track for crossfade/seamless transition.
// ----------------------------------------------------------------------------
void music_queue_track(char* track_name) {
    FUN_8018ba30(track_name);
}

// ----------------------------------------------------------------------------
// 0x8000f588 | music_stop
// Stops music playback (with fadeout handled by driver).
// ----------------------------------------------------------------------------
void music_stop(void) {
    FUN_8018b6f4();
}

// ----------------------------------------------------------------------------
// 0x8000f5a8 | music_start_playback
// Starts music playback with the user's volume setting.
// Checks mute state; if muted, uses alternate start path.
// ----------------------------------------------------------------------------
void music_start_playback(void) {
    DAT_80611700 = DAT_8061100c;
    if (music_is_muted()) {
        FUN_8018b88c(); // Start muted
        DAT_80611700 = 0;
    } else {
        FUN_8018b8bc(); // Start normal
    }
    // Apply computed volume
    music_set_volume(/* computed from DAT_80611700 */);
}

// ----------------------------------------------------------------------------
// 0x8000f620 | music_play_track_id
// Plays a music track by ID. -1 = stop. Handles mute state transitions.
// ----------------------------------------------------------------------------
void music_play_track_id(int track_id) {
    if (track_id == -1 || music_is_channel_muted(track_id)) {
        FUN_8018b7f4(); // Stop with fade
    } else {
        FUN_8018b70c(); // Start track
    }
    FUN_80010108(track_id);
}

// ----------------------------------------------------------------------------
// 0x8000f684 | music_stop_immediate
// Immediately stops all music without fade.
// ----------------------------------------------------------------------------
void music_stop_immediate(void) {
    FUN_8018b7f4();
}

// ----------------------------------------------------------------------------
// 0x8000f6a4 | music_is_paused
// Returns 1 if music is currently paused.
// ----------------------------------------------------------------------------
int music_is_paused(void) {
    return DAT_80611704;
}

// ----------------------------------------------------------------------------
// 0x8000f6ac | music_pause
// Pauses music playback. Sets paused flag and notifies driver.
// ----------------------------------------------------------------------------
void music_pause(void) {
    DAT_80611704 = 1;
    if (!music_is_muted()) {
        FUN_8018b82c();
    }
}

// ----------------------------------------------------------------------------
// 0x8000f6e0 | music_resume
// Resumes paused music playback.
// ----------------------------------------------------------------------------
void music_resume(void) {
    DAT_80611704 = 0;
    if (!music_is_muted()) {
        FUN_8018b85c();
    }
}

// ----------------------------------------------------------------------------
// 0x8000f714 | music_set_volume
// Sets the music playback volume. No-op if music is muted.
// ----------------------------------------------------------------------------
void music_set_volume(double volume) {
    if (!music_is_muted()) {
        FUN_8018b8ec(volume);
    }
}

// ----------------------------------------------------------------------------
// 0x8000f750 | music_get_track_volume
// Returns the effective volume for a given track context.
// Handles special tracks (5-7) with hardware volume, normal tracks with
// level-specific volume tables. Halved on menu levels.
// ----------------------------------------------------------------------------
unsigned int music_get_track_volume(int context) {
    unsigned int volume;
    if (music_is_special_track(context)) {
        if (DAT_80611008 == 0) return 0;
        volume = (unsigned int)(FLOAT_80613710 * (float)(DAT_80611010 - 0x80000000u) * FLOAT_80613714);
    } else {
        // Look up from level volume table
        volume = /* computed from DAT_8061100c and level data */;
        if (music_is_muted()) volume = 0;
        if (DAT_80612614 == 0x65) volume >>= 1; // Half on menu
    }
    return volume;
}

// ----------------------------------------------------------------------------
// 0x8000f8d8 | music_get_current_track
// Returns the ID of the currently playing music track.
// ----------------------------------------------------------------------------
int music_get_current_track(void) {
    return DAT_80612ab0;
}

// ----------------------------------------------------------------------------
// 0x8000f8e0 | music_is_muted
// Returns 1 if music is effectively muted (audio disabled for current level).
// Checks both general audio and level-specific music enable flags.
// ----------------------------------------------------------------------------
int music_is_muted(void) {
    if (FUN_801c2dcc(DAT_80612614)) {
        return (countLeadingZeros(DAT_80611008) >> 5); // 1 if zero (disabled)
    }
    return (countLeadingZeros(DAT_80611004) >> 5);
}

// ----------------------------------------------------------------------------
// 0x8000f928 | music_is_special_track
// Returns 1 if the track ID is a "special" track (IDs 5, 6, or 7).
// Special tracks use hardware volume directly rather than game settings.
// ----------------------------------------------------------------------------
int music_is_special_track(int track_id) {
    if ((unsigned int)(track_id - 5) <= 1 || track_id == 7) {
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 0x8000f964 | music_update_tick
// Per-frame music update. Calls driver stream update.
// ----------------------------------------------------------------------------
void music_update_tick(void) {
    FUN_8018b9f8();
}

// ----------------------------------------------------------------------------
// 0x8000f984 | sound_get_emitter_position
// Gets the 3D position of a sound emitter (character or object).
// Falls back to object position if character data unavailable.
// ----------------------------------------------------------------------------
void sound_get_emitter_position(int* entity, float* out_pos) {
    if (entity == NULL) return;
    int base = *entity;
    if (base == 0) return;

    int char_ptr = *(int*)(base + 0x150);
    if (char_ptr != 0) {
        int model = *(int*)(char_ptr + 8);
        if (model != 0) {
            out_pos[0] = *(float*)(model + 0x27c);
            out_pos[1] = *(float*)(model + 0x280);
            out_pos[2] = *(float*)(model + 0x284);
            return;
        }
    }
    out_pos[0] = *(float*)(base + 100);
    out_pos[1] = *(float*)(base + 0x68);
    out_pos[2] = *(float*)(base + 0x6c);
}

// ----------------------------------------------------------------------------
// 0x8000fa08 | music_stub_nop
// Empty stub.
// ----------------------------------------------------------------------------
void music_stub_nop(void) {
    return;
}

// ----------------------------------------------------------------------------
// 0x8000fa0c | music_is_channel_muted
// Returns true if a specific music channel is muted (bit in mute mask).
// ----------------------------------------------------------------------------
int music_is_channel_muted(int channel) {
    return (DAT_8060faa0 & (1 << channel)) != 0;
}

// ----------------------------------------------------------------------------
// 0x8000fa28 | music_set_channel_track
// Sets the track for a music channel. Handles random track selection
// for tracks in the randomizable range (0x4B-0xAE).
// Updates channel mute mask based on whether track is zero (silence).
// ----------------------------------------------------------------------------
void music_set_channel_track(int channel, int track) {
    if (track > 0x4a && track < 0xaf) {
        track += FUN_80364098(); // Add random offset
    }
    unsigned int mask = DAT_8060faa0 & ~(1 << channel);
    if (track == 0) {
        mask = DAT_8060faa0 | (1 << channel); // Mute
    }
    DAT_8060faa0 = mask;
    FUN_8018b6f8(channel, (short)track);
}

// ----------------------------------------------------------------------------
// 0x8000faa0 | music_select_tracks
// Selects tracks for all 3 music channels.
// param_1: main track override (-1=auto from level table, -2/-3=random)
// param_2: fallback track
// param_3: channel 2 track
// param_4: channel 1 track
// ----------------------------------------------------------------------------
void music_select_tracks(int main_track, int fallback, int ch2_track, int ch1_track) {
    DAT_8060fa98 = fallback;
    if (main_track != -1) {
        DAT_8060fa98 = main_track;
        if (main_track < -1 && main_track > -3) {
            // Random selection from level music table
            int rand_val = FUN_8013104c();
            DAT_8060fa98 = /* table lookup based on rand_val */;
        }
    }
    music_set_channel_track(0, DAT_8060fa98);
    music_set_channel_track(2, ch2_track);
    music_set_channel_track(1, ch1_track);
}
