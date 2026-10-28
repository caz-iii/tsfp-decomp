// Address: 0x8000fb70 - 0x8000fddb | Module: music_level | Date: 2026-08-05
// Music subsystem: level music selection, bank resolution, playback init
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u32  DAT_80612be8;      // Current level resource handle
extern u32  DAT_80612ae8;      // Level type flag (0 = story?)
extern u8   DAT_805015bd;      // Gameplay submode byte
extern u8   DAT_805015c0;      // Gameplay flags byte
extern u32  DAT_80612614;      // Game state ID (100 = arcade match)
extern s32  DAT_80611708;      // Music pak stream handle
extern u32  DAT_8060faa0;      // Music fade state
extern u32  DAT_8048a70c[];    // Music bank-to-track lookup table (stride 8)
extern u32  DAT_8048a6ac[];    // Cutscene music track table
extern u32  DAT_804893a4[];    // Music pak track channel data (stride 0x38)
extern char s_pak_musicts_pak_803ec030[];  // "pak\\musicts.pak"

// External function declarations
extern int*  FUN_80339b90(u32 handle, u32 offset);  // Resource pointer lookup
extern u16   FUN_8031d514(u32 track_raw);            // Track ID conversion
extern void* FUN_8003466c(void);                     // Get current entity/player context
extern void* FUN_803826f0(void);                     // OSDisableInterrupts (returns state)
extern void  FUN_8038273c(void);                     // OSRestoreInterrupts
extern void  FUN_8002a66c(void* state, ...);         // Load pak file async
extern int   FUN_80035410(void);                     // Get loaded pak index
extern int   music_get_current_track(void);
extern void  music_select_tracks(int bank_track, u32 channel_data, int fade_in, int fade_out);
extern void  music_set_channel_track(int channel, int track_data);
extern int   FUN_8022cf74(void);                     // Get cutscene music index

// 0x8000fb70 | music_get_level_track_id
// Returns the converted track ID for the current level's music definition
u16 music_get_level_track_id(void) {
    int* level_data = FUN_80339b90(DAT_80612be8, 0xc77668);
    u16 raw_track = *(u16*)((int)level_data + 10);
    u16 track_id = FUN_8031d514((u32)raw_track);
    return track_id;
}

// 0x8000fba4 | music_get_level_bank_index
// Returns the music bank index for the current level (story mode uses player context)
int music_get_level_bank_index(void) {
    int* level_data = FUN_80339b90(DAT_80612be8, 0xc77668);

    if ((DAT_80612ae8 == 0) || (DAT_805015bd == '\n')) {
        // Default / arcade: use level definition directly
        return (int)*(s16*)((int)level_data + 0xe);
    }

    // Story mode: use player entity's current music state
    u8* entity = FUN_8003466c();
    int bank = *(int*)(entity + 0x114);

    if (*(int*)(&DAT_8048a70c[bank * 2]) == -1) {
        // Invalid bank: fall back to level definition
        return (int)*(s16*)((int)level_data + 0xe);
    }

    return bank;
}

// 0x8000fc30 | music_resolve_bank_to_track
// Resolves a music bank index to its track value. Returns 0 for bank 0, -1 for bank -1.
u32 music_resolve_bank_to_track(int bank) {
    if (bank == 0) {
        return 0;
    }
    if ((bank < 0) && (bank > -2)) {
        return 0xFFFFFFFF;
    }
    return DAT_8048a70c[bank * 2];
}

// 0x8000fc6c | music_init_level_playback
// Loads the music pak and starts level-appropriate music playback
// Handles arcade match mode, story mode, and cutscene music selection
void music_init_level_playback(void) {
    void* int_state = FUN_803826f0();
    DAT_8060faa0 = 0xFFFFFFFF;

    // Load the music pak file
    FUN_8002a66c(int_state, s_pak_musicts_pak_803ec030, 0, 1);

    int pak_index = FUN_80035410();
    DAT_80611708 = pak_index;

    if (DAT_80612614 == 100) {
        // Arcade match mode: select tracks from level definition
        int bank = music_get_level_bank_index();
        int track = music_resolve_bank_to_track(bank);
        u16 track_id = music_get_level_track_id();
        music_select_tracks(track, (u32)track_id, 0x33, 0x2b);
    } else if (pak_index >= 0) {
        if (DAT_80612614 < 0x65) {
            // Story/gameplay mode
            if ((DAT_805015bd != '\n') || ((DAT_805015c0 & 0x40) != 0)) {
                if ((DAT_805015c0 & 0x40) == 0) {
                    // Normal gameplay: use current track's bank
                    int current = music_get_current_track();
                    music_select_tracks(
                        *(int*)(&DAT_8048a70c[current * 2]),
                        DAT_804893a4[pak_index * 0xE],
                        0x33, 0x2b
                    );
                } else {
                    // Cutscene music
                    int cutscene_idx = FUN_8022cf74();
                    music_select_tracks(
                        *(int*)(&DAT_8048a6ac[cutscene_idx]),
                        DAT_804893a4[pak_index * 0xE],
                        0xc1, 0x2b
                    );
                }
                goto done;
            }
        }

        // Fallback: set all 14 channels from pak track data
        for (int ch = 0; ch < 0xe; ch++) {
            music_set_channel_track(ch, *(int*)(&DAT_804893a4[ch + DAT_80611708 * 0xE]));
        }
    }

done:
    FUN_8038273c();  // Restore interrupts
}
