// music_track_player.cpp
// Single-track music / jingle player. A thin control layer over the sound-
// instance engine (FUN_801a1698 play / FUN_801a16ec commit / FUN_801a1fc8 stop
// / FUN_801a2ae4 mute / FUN_801a2d04 resume / FUN_801a2b84 suspend), keyed by
// one global track-instance handle DAT_80612488 (signed char, -1 == none).
// Reached from music_play_track_id / music_load_bank.
//
// Batch: batch_490
#include "common.h"

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Sound-instance engine control (indexed by track handle).
extern void FUN_801a1698(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int bankId,
                         unsigned int a10, unsigned int a11, int a12, int a13,
                         unsigned int a14, unsigned int a15, unsigned int a16);
extern unsigned int FUN_801a16ec(int *pTrackTable, int packedFrame, int centibels);
extern void FUN_801a1fc8(int handle);   // stop
extern void FUN_801a2ae4(int handle);   // mute / silence
extern void FUN_801a2d04(int handle);   // resume from mute
extern void FUN_801a2b84(int handle);   // suspend / duck
extern void FUN_801a2c24(int handle);   // clear suspend flag (un-suspend)
extern unsigned int FUN_801a2aac(int handle); // query: instance active?
extern void FUN_801a2c4c(void);         // apply pending volume/centibels
// Engine track (re)build / prepare / blocking-fade helpers (16-arg tails).
extern void FUN_801a15fc(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int a9, int a10,
                         char *a11, int a12, int a13, unsigned int a14, unsigned int a15,
                         unsigned int a16);
extern void FUN_801a161c(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int a9, int a10,
                         char *a11, int a12, int a13, unsigned int a14, unsigned int a15,
                         unsigned int a16);
extern void FUN_801a2fe0(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int a9, int a10,
                         char *a11, int a12, int a13, unsigned int a14, unsigned int a15,
                         unsigned int a16);

// Music / audio helpers.
extern unsigned int music_get_track_volume(int track);
extern int  audio_linear_to_centibels(int linear);
extern int  dvd_get_file_size(double f1, double f2, double f3, unsigned long long a4,
                              unsigned long long a5, unsigned long long a6,
                              unsigned long long a7, unsigned long long a8);

// Frame-guard pair (the *f0 variant returns a packed record: hi word = track).
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);

// Global audio mutex guard pair.
extern void FUN_8039cca0(void);   // lock
extern void FUN_8039ccc8(void);   // unlock

// memset shim.
extern void memset_wrapper(int dst, int val, int len);

// ---- Data ----
extern signed char   DAT_80612488;   // active music-track instance handle (-1 = none)
extern int           DAT_805ac9c0;   // per-track bank-id table (u16 entries)
extern unsigned short DAT_80610ac8;  // last-updated track index (u16)
extern float         FLOAT_80610acc; // volume-fraction -> file-position scale
extern float         FLOAT_8061680c; // centibel divisor
extern float         FLOAT_80616810; // volume-scale coefficient
extern double        DOUBLE_806167f0;// unsigned->double bias constant
extern int           PTR_DAT_80488d54; // table of track-parameter block pointers
extern int           DAT_80612518;  // pending track count (used by engine rebuild)
}

// ---------------------------------------------------------------------------
// Address: 0x8018b6c8 | Name: music_track_play | Date: 2026-09-06
// Start playback of a music bank. Marks the track handle as pending (0xff) and
// hands off to the sound-instance engine with the "music" mode flag (arg9 = 1).
// ---------------------------------------------------------------------------
void music_track_play(unsigned long long param_1, unsigned long long param_2,
                      unsigned long long param_3, unsigned long long param_4,
                      unsigned long long param_5, unsigned long long param_6,
                      unsigned long long param_7, unsigned long long param_8,
                      unsigned int param_9, unsigned int param_10,
                      unsigned int param_11, int param_12, int param_13,
                      unsigned int param_14, unsigned int param_15,
                      unsigned int param_16)
{
    DAT_80612488 = (signed char)0xff;
    FUN_801a1698(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ---------------------------------------------------------------------------
// Address: 0x8018b6f4 | Name: music_track_noop | Date: 2026-09-06
// Empty hook (retained for call-site / vtable parity).
// ---------------------------------------------------------------------------
void music_track_noop(void)
{
}

// ---------------------------------------------------------------------------
// Address: 0x8018b6f8 | Name: music_track_set_bank_id | Date: 2026-09-06
// Store the bank id for a track slot into the per-track bank-id table.
// ---------------------------------------------------------------------------
void music_track_set_bank_id(int track, unsigned short bankId)
{
    *(unsigned short *)((char *)&DAT_805ac9c0 + track * 2) = bankId;
}

// ---------------------------------------------------------------------------
// Address: 0x8018b70c | Name: music_track_update | Date: 2026-09-06
// Per-frame music-track service. Stops any currently-playing instance, then
// re-queues the track: derive a file position from the track volume, clamp to a
// default param block (index 0x3c) when the DVD file size is unknown, convert
// the level to centibels, and commit the new instance via FUN_801a16ec.
// ---------------------------------------------------------------------------
void music_track_update(void)
{
    unsigned long long frame = FUN_803826f0();
    int track = (int)(frame >> 0x20);

    if (DAT_80612488 >= 0) {
        FUN_801a1fc8((int)DAT_80612488);
    }
    DAT_80610ac8 = (unsigned short)(frame >> 0x20);

    unsigned int vol = music_get_track_volume(track);
    unsigned int paramIdx = (unsigned int)*(unsigned short *)((char *)&DAT_805ac9c0 + track * 2);

    // Reconstruct the float sequence the compiler emitted (unsigned->double bias).
    double volPos = (double)(float)((double)(vol * 0x3fff) - 0.0);
    double filePos = (double)(float)((double)FLOAT_80610acc * volPos);

    int size = dvd_get_file_size(filePos, volPos, DOUBLE_806167f0, 0, 0, 0, 0, 0);
    if (size == -1) {
        paramIdx = 0x3c;
    }

    int centibels = audio_linear_to_centibels((int)(filePos / (double)FLOAT_8061680c));
    unsigned int handle = FUN_801a16ec((int *)(&PTR_DAT_80488d54)[paramIdx], (int)frame, centibels);
    DAT_80612488 = (signed char)handle;
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Address: 0x8018b7f4 | Name: music_track_stop | Date: 2026-09-06
// Stop the active music track and release the handle.
// ---------------------------------------------------------------------------
void music_track_stop(void)
{
    if (DAT_80612488 >= 0) {
        FUN_801a1fc8((int)DAT_80612488);
        DAT_80612488 = -1;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b82c | Name: music_track_mute | Date: 2026-09-06
// Silence the active music track (pauses both AX channels) without releasing it.
// ---------------------------------------------------------------------------
void music_track_mute(void)
{
    if (DAT_80612488 >= 0) {
        FUN_801a2ae4((int)DAT_80612488);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b85c | Name: music_track_resume | Date: 2026-09-06
// Resume the active music track from a muted/suspended state.
// ---------------------------------------------------------------------------
void music_track_resume(void)
{
    if (DAT_80612488 >= 0) {
        FUN_801a2d04((int)DAT_80612488);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b88c | Name: music_track_suspend | Date: 2026-09-06
// Suspend / duck the active music track (e.g. while another stream takes over).
// ---------------------------------------------------------------------------
void music_track_suspend(void)
{
    if (DAT_80612488 >= 0) {
        FUN_801a2b84((int)DAT_80612488);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b8bc | Name: music_track_unsuspend | Date: 2026-09-06
// Clear the suspend/duck state on the active music track (FUN_801a2c24 sets the
// suspend flag DAT_80612510 back to 0).
// ---------------------------------------------------------------------------
void music_track_unsuspend(void)
{
    if (DAT_80612488 >= 0) {
        FUN_801a2c24((int)DAT_80612488);
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b8ec | Name: music_track_set_volume | Date: 2026-09-06
// Set the master music-volume scale (FLOAT_80610acc). When it actually changes
// and a track is live, recompute the target level in centibels from the current
// track volume and push it to the engine (FUN_801a2c4c) under the audio lock.
// Caller: music_set_volume.
// ---------------------------------------------------------------------------
void music_track_set_volume(double scale)
{
    if ((double)FLOAT_80610acc != scale) {
        if (DAT_80612488 >= 0) {
            unsigned int vol = music_get_track_volume((int)DAT_80610ac8);
            float level = (FLOAT_80616810 *
                           (float)(scale *
                                   (double)(float)((double)vol - DOUBLE_806167f0))) / FLOAT_8061680c;
            FUN_8039cca0();
            audio_linear_to_centibels((int)level);
            FUN_801a2c4c();
            FUN_8039ccc8();
        }
        FLOAT_80610acc = (float)scale;
    }
}

// ---------------------------------------------------------------------------
// Address: 0x8018b9b0 | Name: music_track_prepare | Date: 2026-09-06
// Stop the current instance, then run the engine buffer-prep tail (FUN_801a15fc)
// for a new track described by the trailing arguments.
// ---------------------------------------------------------------------------
void music_track_prepare(unsigned long long param_1, unsigned long long param_2,
                         unsigned long long param_3, unsigned long long param_4,
                         unsigned long long param_5, unsigned long long param_6,
                         unsigned long long param_7, unsigned long long param_8,
                         unsigned int param_9, unsigned int param_10, char *param_11,
                         int param_12, int param_13, unsigned int param_14,
                         unsigned int param_15, unsigned int param_16)
{
    music_track_stop();
    FUN_801a15fc(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 0, 0, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ---------------------------------------------------------------------------
// Address: 0x8018b9d4 | Name: music_track_restart | Date: 2026-09-06
// Prepare a fresh track (music_track_prepare) then allocate its streaming
// scratch and start it (FUN_801a161c).
// ---------------------------------------------------------------------------
void music_track_restart(unsigned long long param_1, unsigned long long param_2,
                         unsigned long long param_3, unsigned long long param_4,
                         unsigned long long param_5, unsigned long long param_6,
                         unsigned long long param_7, unsigned long long param_8,
                         unsigned int param_9, unsigned int param_10, char *param_11,
                         int param_12, int param_13, unsigned int param_14,
                         unsigned int param_15, unsigned int param_16)
{
    music_track_prepare(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        param_9, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
    FUN_801a161c(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 0, 0, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ---------------------------------------------------------------------------
// Address: 0x8018b9f8 | Name: music_track_is_playing | Date: 2026-09-06
// Return non-zero when a track handle is allocated and its engine instance is
// active (FUN_801a2aac). Caller: music_update_tick.
// ---------------------------------------------------------------------------
unsigned int music_track_is_playing(void)
{
    if (DAT_80612488 < 0) {
        return 0;
    }
    return FUN_801a2aac((int)DAT_80612488);
}

// ---------------------------------------------------------------------------
// Address: 0x8018ba30 | Name: music_track_stop_blocking | Date: 2026-09-06
// Stop the current instance, then run the blocking fade-out / retrace-synced
// teardown tail (FUN_801a2fe0).
// ---------------------------------------------------------------------------
void music_track_stop_blocking(unsigned long long param_1, unsigned long long param_2,
                               unsigned long long param_3, unsigned long long param_4,
                               unsigned long long param_5, unsigned long long param_6,
                               unsigned long long param_7, unsigned long long param_8,
                               unsigned int param_9, unsigned int param_10, char *param_11,
                               int param_12, int param_13, unsigned int param_14,
                               unsigned int param_15, unsigned int param_16)
{
    music_track_stop();
    FUN_801a2fe0(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                 0, 0, param_11, param_12, param_13, param_14, param_15, param_16);
}
