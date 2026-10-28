// Address: 0x8000fddc - 0x80010124 | Module: sound_bank | Date: 2026-08-05
// Sound subsystem: bank table management, volume control, sample resolution
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u8   DAT_805015be;      // Sound mode byte
extern u32  DAT_805015c0;      // Gameplay flags
extern u32  DAT_8061156c;      // Music override flag
extern u32  DAT_80612614;      // Game state ID
extern u32  DAT_803ec040[];    // Sound bank definition table (stride 0xC: mode, range_start, sample_id)
extern u32  DAT_804cd9f0[];    // Sound bank table A (SFX)
extern u32  DAT_804cd9fc[];    // Sound bank table B (music/voice)
extern f32  FLOAT_8060faa8;    // Master volume
extern f32  FLOAT_8060faac;    // Computed SFX volume (product of scales)
extern f32  FLOAT_8060fab0;    // SFX base volume scale
extern f32  FLOAT_8060fab4;    // Music volume
extern f32  FLOAT_8060fab8;    // Voice volume scale
extern u32  DAT_8060fabc;      // SFX volume raw setting
extern u32  DAT_8060fac0;      // Voice volume raw setting
extern u32  DAT_80611710;      // Sound mute flag
extern f32  FLOAT_80613738;    // Default master volume
extern f32  FLOAT_8061373c;    // Default music volume
extern f32  FLOAT_80613740;    // Default SFX/voice volume (1.0)

// External function declarations
extern int  FUN_80035bb4(u32 state_id);       // Get level config by state
extern int  FUN_800102d0(void);               // Get music override track
extern void music_play_track_id(int track);
extern void sound_clear_bank_ref(int sample_id);
extern void* FUN_803826e4(void);              // OSDisableInterrupts
extern void  FUN_80382730(void);              // OSRestoreInterrupts
extern void* mem_alloc(int size, int align, ...);

// 0x8000fddc | music_play_level_track
// Selects and plays the appropriate music track for the current level state
void music_play_level_track(void) {
    if ((DAT_805015c0 & 0x20000000) != 0) {
        return;  // Music disabled by flag
    }

    int track;
    if (DAT_8061156c == 0) {
        // Normal: look up track from level config
        int level_cfg = FUN_80035bb4(DAT_80612614);
        if (level_cfg == 0) {
            track = 0;
        } else {
            track = *(int*)(level_cfg + 0x120);
        }
    } else {
        // Override active
        track = FUN_800102d0();
    }

    music_play_track_id(track);
}

// 0x8000fe4c | sound_build_bank_table
// Builds a lookup table mapping sample range to sound bank IDs for a given mode.
// Scans the 0x83E-entry master table, finds min/max range for the mode,
// allocates array, and populates with sample IDs.
// param_9: mode to match, param_10: output struct ptr, param_11: whether to clear refs
void sound_build_bank_table(int mode, int* out_table, int clear_refs) {
    FUN_803826e4();  // Disable interrupts

    int max_range = 0;
    out_table[2] = 0x1D57;  // Initial min = large value

    // First pass: find range bounds
    for (int i = 0; i < 0x83E; i++) {
        int* entry = (int*)((int)DAT_803ec040 + i * 0xC);
        if (mode == entry[0]) {
            int range_start = entry[1];
            if (range_start < out_table[2]) {
                out_table[2] = range_start;
            }
            if (max_range < range_start) {
                max_range = range_start;
            }
        }
    }

    // Calculate table size
    out_table[1] = (max_range - out_table[2]) + 1;

    if (out_table[1] < 1) {
        out_table[1] = 0;
        out_table[0] = 0;
    } else {
        // Allocate and initialize to -1 (invalid)
        int alloc_size = (out_table[1] * 4 + 0xF) & 0xFFFFFFF0;  // 16-byte aligned
        out_table[0] = (int)mem_alloc(alloc_size, 1);

        for (int i = 0; i < out_table[1]; i++) {
            *(u32*)(out_table[0] + i * 4) = 0xFFFFFFFF;
        }

        // Second pass: populate table
        for (int i = 0; i < 0x83E; i++) {
            int* entry = (int*)((int)DAT_803ec040 + i * 0xC);
            if (mode == entry[0]) {
                int idx = entry[1] - out_table[2];
                *(u32*)(out_table[0] + idx * 4) = (u32)entry[2];

                if (clear_refs != 0) {
                    sound_clear_bank_ref(entry[2]);
                }
            }
        }
    }

    FUN_80382730();  // Restore interrupts
}

// 0x8000ffc4 | sound_init_bank_tables
// Initializes both SFX and music/voice sound bank lookup tables
void sound_init_bank_tables(void) {
    int mode;
    int clear_music;

    if (DAT_805015be == 0x10) {
        mode = 0xFF;
        clear_music = 0;  // Don't clear refs for mode 0xFF
    } else {
        mode = 0xFE;
        clear_music = 1;
    }

    sound_build_bank_table(mode, (int*)DAT_804cd9f0, 1);
    sound_build_bank_table(mode, (int*)DAT_804cd9fc, clear_music ? 0 : 1);
}

// 0x8001003c | sound_bank_lookup
// Looks up a sample ID in a bank table. Returns -1 if out of range.
u32 sound_bank_lookup(int sample_index, int* bank_table) {
    int offset = sample_index - bank_table[2];

    if (offset < 0) {
        return 0xFFFFFFFF;
    }
    if (offset >= bank_table[1]) {
        return 0xFFFFFFFF;
    }

    return *(u32*)(bank_table[0] + offset * 4);
}

// 0x80010074 | sound_resolve_sample_id
// Resolves a sound sample index to its bank ID, checking both SFX and voice tables
int sound_resolve_sample_id(int sample_index) {
    if (sample_index == 0) {
        return -1;
    }

    int result = sound_bank_lookup(sample_index, (int*)DAT_804cd9f0);
    if (result == -1) {
        result = sound_bank_lookup(sample_index, (int*)DAT_804cd9fc);
    }
    return result;
}

// 0x800100cc | sound_volume_reset_defaults
// Resets all volume levels to their default constants
void sound_volume_reset_defaults(void) {
    FLOAT_8060faa8 = FLOAT_80613738;    // Master volume = default
    FLOAT_8060fab4 = FLOAT_8061373c;    // Music volume = default
    FLOAT_8060faac = FLOAT_80613740;    // SFX computed = 1.0
    FLOAT_8060fab0 = FLOAT_80613740;    // SFX base = 1.0
    FLOAT_8060fab8 = FLOAT_80613740;    // Voice scale = 1.0
    DAT_80611710 = 0;                    // Unmute
}

// 0x800100f8 | sound_set_master_volume
// Sets the master volume level
void sound_set_master_volume(f32 volume) {
    FLOAT_8060faa8 = volume;
}

// 0x80010100 | sound_set_mute_flag
// Sets the global sound mute flag
void sound_set_mute_flag(u32 muted) {
    DAT_80611710 = muted;
}

// 0x80010108 | sound_set_sfx_volume_scale
// Sets the SFX volume scale and recalculates computed volume
void sound_set_sfx_volume_scale(u32 scale) {
    DAT_8060fabc = scale;
    DAT_8060fac0 = scale;
    FLOAT_8060faac = FLOAT_8060fab0 * FLOAT_8060fab8;
}

// 0x80010124 | sound_set_voice_volume_scale
// Sets the voice volume scale
void sound_set_voice_volume_scale(u32 scale) {
    DAT_8060fac0 = scale;
}


// Address: 0x80146bf4 | Name: entity_release_sound_banks | Date: 2026-09-05 (batch_412)
// Release every sound-bank reference owned by the current spawn-frame entity.
// Enumerates the entity's two bank ranges (primary via FUN_802523ac, secondary
// via FUN_802532b0), clears each referenced sample via sound_clear_bank_ref,
// then tears down the entity's bank bookkeeping (FUN_80253400 / FUN_80253880).
// Driven by the sub-object spawn frame (FUN_803826f0 / FUN_8038273c).
extern "C" {
    extern unsigned long long FUN_803826f0(void);   // push spawn frame -> packed ptr|handle<<32
    extern void               FUN_8038273c(void);    // pop spawn frame
    extern int  FUN_802523ac(int handle, int entity, int* outRange, int* outEnd, void* unused);
    extern int  FUN_802532b0(int handle, int* outRange, int* outEnd, void* unused);
    extern void FUN_80253400(int handle);            // release bank table A
    extern void FUN_80253880(int handle);            // release bank table B
}

void entity_release_sound_banks(void) {
    unsigned long long frame;
    int handle;
    int ok;
    int range[5];
    int end;
    int i;

    frame  = FUN_803826f0();
    handle = (int)(frame >> 0x20);

    ok = FUN_802523ac(handle, (int)frame, range, &end, (void*)0);
    if (ok != 0) {
        for (i = range[0]; i <= end; ++i) {
            sound_clear_bank_ref(i);
        }
    }

    ok = FUN_802532b0(handle, range, &end, (void*)0);
    if (ok != 0) {
        for (; range[0] <= end; ++range[0]) {
            sound_clear_bank_ref(range[0]);
        }
    }

    FUN_80253400(handle);
    FUN_80253880(handle);
    FUN_8038273c();
}
