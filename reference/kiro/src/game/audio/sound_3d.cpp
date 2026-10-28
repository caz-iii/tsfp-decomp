// Address: 0x8001012c - 0x80010898 | Module: sound_3d | Date: 2026-08-05
// Sound subsystem: 3D positional audio, volume fading, music volume management
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern f32  FLOAT_8060fab0;    // SFX base volume scale
extern f32  FLOAT_8060fab4;    // Music volume (last set)
extern f32  FLOAT_8060fab8;    // Voice volume scale
extern f32  FLOAT_8060faac;    // SFX computed volume
extern f32  FLOAT_8060faa8;    // Master volume
extern u32  DAT_8060fabc;      // SFX volume raw setting
extern u32  DAT_8060fac0;      // Voice volume raw setting (pending track)
extern u32  DAT_8060fac4;      // Saved track ID
extern u32  DAT_80611710;      // Sound mute flag
extern f32  FLOAT_80611718;    // Global listener volume scale
extern u32  DAT_804cda08[];    // Fade slot handles (10 slots, stride 0x1C)
extern u32  DAT_804cdb20[];    // Looping sound handles (6 slots, stride 0xC)
extern u32  DAT_80612610;      // Active entity count
extern u32  DAT_80611d78;      // Entity array base
extern u32  DAT_80611d74;      // Entity system handle
extern f32  FLOAT_806108e8;    // Frame delta time (float)
extern f32  FLOAT_806108ec;    // Frame delta time (float, variant)
extern u32  DAT_8060fa8c;      // Sound definition table base

// Constants
extern f32  FLOAT_80613738;    // Default master volume
extern f32  FLOAT_8061373c;    // Default music volume
extern f32  FLOAT_80613740;    // 1.0 (unity scale)
extern f32  FLOAT_80613744;    // Distance volume curve factor
extern f32  FLOAT_80613748;    // 0.0 (silence / min volume)
extern f32  FLOAT_8061374c;    // Volume curve offset
extern f32  FLOAT_80613750;    // Volume curve scale
extern f32  FLOAT_80613754;    // Music silence threshold
extern f32  FLOAT_80613758;    // Music volume change epsilon
extern f32  FLOAT_80613760;    // 3D source audibility threshold
extern f32  FLOAT_80613764;    // Default fade volume (full)
extern f32  FLOAT_80613770;    // Default listener volume
extern f32  FLOAT_80613774;    // Muffled listener volume
extern f32  FLOAT_80613778;    // Listener lerp speed
extern f64  DOUBLE_80613768;   // Double conversion constant

// External function declarations
extern f64  FUN_80131538(f64 value);       // Clamp/curve function
extern void sound_fade_master_volume(f64 volume);
extern int  music_is_paused(void);
extern int  music_update_tick(void);
extern void FUN_8012edfc(f64 target, f64 speed, f32* out_value);  // Lerp/smooth
extern void music_play_track_id(u32 track);
extern void music_pause(void);
extern void music_resume(void);
extern void music_set_volume(f64 volume);
extern void sound_update_3d_volume(f64 volume, u32 handle, u32* position);
extern void sound_set_pitch(f64 pitch, u32 handle);
extern int  sound_is_position_in_range(f64 range, f32* position);
extern u32  FUN_80010dfc(f64 range, f64 pitch, f64 volume, int sample, f32* pos, int flags);
extern void FUN_80010c94(int sample, u32* handle, u32 has_position);
extern void sound_stop_handle(void);
extern int  sound_is_playing(u32 handle);
extern void sound_init_play_params(int params_ptr);
extern u32  sound_play_request(int* params);
extern void sound_set_3d_position(u32 handle, u32 param1, u32 param2);
extern int  FUN_8036fe90(u32 handle);
extern void* FUN_803826e8(void);
extern void* FUN_803826ec(void);
extern void* FUN_803826f0(void);
extern void  FUN_80382734(void);
extern void  FUN_80382738(void);
extern void  FUN_8038273c(void);

// 0x8001012c | sound_calc_distance_volume
// Calculates volume attenuation based on listener distance (clamped curve)
void sound_calc_distance_volume(f64 distance) {
    f64 clamped = FUN_80131538(distance);
    f64 value = clamped;

    // Clamp to [0.0, 1.0]
    if (clamped > (f64)FLOAT_80613740) {
        value = (f64)FLOAT_80613740;
    }
    if (value < (f64)FLOAT_80613748) {
        value = (f64)FLOAT_80613748;
    } else if (clamped > (f64)FLOAT_80613740) {
        value = (f64)FLOAT_80613740;
    }

    // Apply volume curve: -(factor * value - 1.0)
    FLOAT_8060fab0 = -(f32)((f64)FLOAT_80613744 * value - (f64)FLOAT_80613740);
}

// 0x80010198 | sound_set_voice_base_volume
// Sets the base voice volume multiplier
void sound_set_voice_base_volume(f32 volume) {
    FLOAT_8060fab8 = volume;
}

// 0x800101a0 | sound_update_music_volume
// Per-frame music volume update: handles fading, track changes, muting
void sound_update_music_volume(void) {
    // Apply distance-based master volume curve
    sound_fade_master_volume((f64)(FLOAT_80613750 * FLOAT_8060fab0 + FLOAT_8061374c));

    int paused = music_is_paused();
    if (paused == 0) {
        // Music is playing
        int tick_active = music_update_tick();

        // Determine target volume
        int volume_active = (DAT_8060fac0 == DAT_8060fabc) && (DAT_80611710 == 0);
        f32 target_vol = FLOAT_80613748;  // Default: silence
        if (volume_active) {
            target_vol = FLOAT_8060fab0 * FLOAT_8060fab8;
        }

        // Smooth towards target if tick is active
        if (tick_active != 0) {
            FUN_8012edfc((f64)target_vol, (f64)FLOAT_8060faa8, &FLOAT_8060faac);
            target_vol = FLOAT_8060faac;
        }
        FLOAT_8060faac = target_vol;

        // If volume reached silence, handle track change or pause
        if (FLOAT_8060faac <= FLOAT_80613754) {
            if (DAT_8060fac0 != DAT_8060fabc) {
                music_play_track_id(DAT_8060fac0);
            }
            if (DAT_80611710 != 0) {
                music_pause();
            }
        }

        // Apply volume to music if change is significant enough
        f32 diff = FLOAT_8060faac - FLOAT_8060fab4;
        if (diff < 0) diff = -diff;
        if ((diff >= FLOAT_80613758) && (tick_active != 0)) {
            music_set_volume((f64)(FLOAT_8061373c * FLOAT_8060faac));
            FLOAT_8060fab4 = FLOAT_8060faac;
        }
    } else if (DAT_80611710 == 0) {
        // Music paused but not muted: resume
        music_resume();
    }
}

// 0x800102d0 | music_get_pending_track
// Returns the pending track ID that hasn't been applied yet
u32 music_get_pending_track(void) {
    return DAT_8060fac4;
}

// 0x800102d8 | music_save_current_track
// Saves the current voice volume track as the pending track
void music_save_current_track(void) {
    DAT_8060fac4 = DAT_8060fac0;
}

// 0x800102e4 | sound_update_3d_source
// Updates a 3D sound source: creates, updates position/volume, or stops based on range
// handle_ptr: pointer to sound handle (-1 = inactive)
// position: 3D position (NULL = 2D)
// sample_id: sound sample to play
// flags: bit 0 = update pitch, bit 1 = fade instead of stop, bit 2 = force play
void sound_update_3d_source(f64 range, f64 pitch, f64 volume,
                            int sample_id, u32* handle_ptr, f32* position, u32 flags) {
    FUN_803826ec();  // Disable interrupts

    int force_play = (flags >> 2) & 1;

    if (sample_id < 0) goto done;

    if ((force_play != 0) || (volume > (f64)FLOAT_80613760)) {
        // Sound should be audible
        if (position == NULL) {
            // 2D sound
            if ((int)*handle_ptr < 0) {
                *handle_ptr = FUN_80010dfc((f64)FLOAT_80613764, pitch, volume, sample_id, NULL, 0);
            } else {
                sound_update_3d_volume(volume, *handle_ptr, NULL);
                if (((flags & 1) != 0) && ((int)*handle_ptr >= 0)) {
                    sound_set_pitch(pitch, *handle_ptr);
                }
            }
        } else {
            // 3D sound
            if ((force_play != 0) || sound_is_position_in_range(range, position)) {
                if ((int)*handle_ptr < 0) {
                    *handle_ptr = FUN_80010dfc(range, pitch, volume, sample_id, position, 0);
                } else {
                    sound_update_3d_volume(volume, *handle_ptr, position);
                    if (((flags & 1) != 0) && ((int)*handle_ptr >= 0)) {
                        sound_set_pitch(pitch, *handle_ptr);
                    }
                }
            } else {
                goto out_of_range;
            }
        }
        goto done;
    }

out_of_range:
    // Sound out of range or too quiet
    if ((int)*handle_ptr >= 0) {
        if ((flags & 2) == 0) {
            sound_stop_handle();
            *handle_ptr = 0xFFFFFFFF;
        } else {
            FUN_80010c94(sample_id, handle_ptr, (u32)(position != NULL));
        }
    }

done:
    FUN_80382738();  // Restore interrupts
}

// 0x800104a4 | sound_play_stereo_pair
// Plays a stereo pair of sounds at different pan positions
void sound_play_stereo_pair(f64 pan_left, f64 pan_right, f64 volume) {
    int params_a[6];  // play params struct (local_b8)
    int params_b[6];  // play params struct (local_88)

    int sound_def_idx = (int)FUN_803826f0();
    u32 base_pan = (u32)*(u8*)(DAT_8060fa8c + sound_def_idx * 0x14 + 5);

    // First sound (left channel)
    sound_init_play_params((int)params_b);
    *(f32*)&params_b[5] = (f32)pan_left;
    params_b[0] = sound_def_idx;
    u32 handle_a = sound_play_request(params_b);

    // Second sound (right channel)
    sound_init_play_params((int)params_a);
    *(f32*)&params_a[5] = (f32)pan_right;
    params_a[0] = sound_def_idx;
    u32 handle_b = sound_play_request(params_a);

    // Apply 3D panning
    if ((int)handle_a >= 0) {
        u32 scaled = (u32)((f32)((f64)base_pan - DOUBLE_80613768) * (f32)volume);
        sound_set_3d_position(handle_a, base_pan, scaled);
    }
    if ((int)handle_b >= 0) {
        int scaled = (int)((f32)((f64)base_pan - DOUBLE_80613768) * (f32)volume);
        sound_set_3d_position(handle_b, scaled, base_pan);
    }

    FUN_8038273c();  // Restore interrupts
}

// 0x800105d8 | sound_init_fade_slots
// Initializes 10 fade-out slots and 6 looping sound slots to inactive state
void sound_init_fade_slots(void) {
    f32 default_vol = FLOAT_80613764;

    // Initialize 10 fade slots (stride 0x1C each)
    for (int i = 0; i < 10; i++) {
        *(u32*)((int)DAT_804cda08 + i * 0x1C) = 0xFFFFFFFF;  // handle = invalid
    }

    FLOAT_80611718 = FLOAT_80613770;  // Reset listener volume

    // Initialize 6 looping sound slots (stride 0xC each)
    for (int i = 0; i < 6; i++) {
        int offset = i * 0xC;
        *(u32*)((int)DAT_804cdb20 + offset) = 0xFFFFFFFF;      // handle = invalid
        *(f32*)((int)DAT_804cdb20 + offset + 4) = default_vol;  // volume = default
        *(f32*)((int)DAT_804cdb20 + offset + 8) = default_vol;  // target = default
    }
}

// 0x8001063c | sound_register_fade_out
// Registers a playing sound for gradual fade-out. Transfers ownership from caller.
// fade_rate: how quickly to fade, target_vol: final volume before stop
void sound_register_fade_out(f64 fade_rate, f64 target_vol, int* handle_ptr, u32* position) {
    if (*handle_ptr < 0) return;

    // Find empty fade slot
    for (int i = 0; i < 10; i++) {
        int offset = i * 0x1C;
        if (*(int*)((int)DAT_804cda08 + offset) < 0) {
            // Slot available: register
            *(int*)((int)DAT_804cda08 + offset + 0x00) = *handle_ptr;       // handle
            *(f32*)((int)DAT_804cda08 + offset + 0x04) = (f32)fade_rate;    // current volume
            *(f32*)((int)DAT_804cda08 + offset + 0x08) = (f32)target_vol;   // fade speed
            *(u16*)((int)DAT_804cda08 + offset + 0x0C) = 0;                 // flags

            if (position != NULL) {
                // Copy 3D position (12 bytes)
                *(u32*)((int)DAT_804cda08 + offset + 0x10) = position[0];
                *(u32*)((int)DAT_804cda08 + offset + 0x14) = position[1];
                *(u32*)((int)DAT_804cda08 + offset + 0x18) = position[2];
                *(u16*)((int)DAT_804cda08 + offset + 0x0C) |= 1;  // has position flag
            }

            *handle_ptr = -1;  // Transfer ownership
            return;
        }
    }

    // No slot available: force stop
    sound_stop_handle();
    *handle_ptr = -1;
}

// 0x80010700 | sound_update_fade_slots
// Per-frame update: fades out registered sounds, updates listener volume, stops silent ones
void sound_update_fade_slots(void) {
    FUN_803826e8();  // Disable interrupts

    f64 default_vol = (f64)FLOAT_80613764;

    // Calculate average listener environment volume
    f64 total_weighted = default_vol;
    f64 total_count = default_vol;
    for (int i = 0; i < (int)DAT_80612610; i++) {
        int entity_offset = i * 0x141C;
        if (*(int*)(DAT_80611d78 + entity_offset + 4) >= 0) {
            f64 weight = (f64)FLOAT_80613770;

            // Check if entity is of type 0x2000 with muffling flag
            if ((*(int*)(DAT_80611d78 + entity_offset + 0x114) == 0x2000) &&
                (FUN_8036fe90(DAT_80611d74) != 0)) {
                int info = FUN_8036fe90(DAT_80611d74);
                if ((*(u16*)(*(int*)(info + 0x7C) + 0x1A) & 4) != 0) {
                    weight = (f64)FLOAT_80613774;  // Muffled
                }
            }
            total_weighted += weight;
            total_count += (f64)FLOAT_80613770;
        }
    }

    // Smooth listener volume toward average
    if (total_count != default_vol) {
        f64 avg = total_weighted / total_count;
        FUN_8012edfc(avg, (f64)(FLOAT_80613778 * FLOAT_806108ec), &FLOAT_80611718);
    }

    // Update each fade slot
    for (int i = 0; i < 10; i++) {
        int offset = i * 0x1C;
        u32* p_handle = (u32*)((int)DAT_804cda08 + offset);

        if ((int)*p_handle < 0) continue;

        // Apply fade: current_vol -= dt * fade_speed
        f32* p_current = (f32*)((int)DAT_804cda08 + offset + 0x04);
        f32* p_speed = (f32*)((int)DAT_804cda08 + offset + 0x08);
        *p_current = -(*p_speed * FLOAT_806108e8 - *p_current);

        // Check if sound is still playing
        if (!sound_is_playing(*p_handle)) {
            *p_handle = 0xFFFFFFFF;
        } else {
            f64 vol = (f64)*p_current;
            if (vol > (f64)FLOAT_80613764) {
                // Still audible: update volume
                u16 flags = *(u16*)((int)DAT_804cda08 + offset + 0x0C);
                if ((flags & 1) == 0) {
                    sound_update_3d_volume(vol, *p_handle, NULL);
                } else {
                    sound_update_3d_volume(vol, *p_handle, (u32*)((int)DAT_804cda08 + offset + 0x10));
                }
            } else {
                // Below threshold: stop
                sound_stop_handle();
                *p_handle = 0xFFFFFFFF;
            }
        }
    }

    FUN_80382734();  // Restore interrupts
}
