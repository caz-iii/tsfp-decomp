// Address: 0x800108b8 - 0x80010fac | Module: sound_persistent | Date: 2026-08-05
// Sound subsystem: persistent/looping sounds, ambient 3D, play-or-resume logic
// Decompiled from main.dol (GameCube - TimeSplitters: Future Perfect)

#include <types.h>

// External data references
extern u32  DAT_804cdb20[];    // Looping sound slots (6, stride 0x0C: handle, volume, age)
extern u32  DAT_804cdb68[];    // Persistent sound slots (4, stride 0x0C: sound_id, handle, flags)
extern u32  DAT_803cc0d0;      // Default 3D position X
extern u32  DAT_803cc0d4;      // Default 3D position Y
extern u32  DAT_803cc0d8;      // Default 3D position Z
extern f32  FLOAT_8061377c;    // Looping slot age init (large)
extern f32  FLOAT_80613780;    // Looping slot age reset (small, for recycled)
extern f32  FLOAT_80613784;    // Looping slot age decay rate
extern f32  FLOAT_80613788;    // Fade-out target for recycled looping sound
extern f32  FLOAT_8061378c;    // Ambient pitch base
extern f32  FLOAT_80613790;    // Ambient pitch range
extern f32  FLOAT_80613794;    // Random scale divisor
extern f32  FLOAT_806137a0;    // Persistent 3D range
extern f32  FLOAT_806137a4;    // Persistent 3D pitch
extern f32  FLOAT_806137a8;    // Persistent volume (no position)
extern f32  FLOAT_806137ac;    // Default play volume / pitch
extern f32  FLOAT_806137b0;    // Looping play volume
extern f64  DOUBLE_80613798;   // Random offset double constant

// External function declarations
extern int   sound_is_playing(u32 handle);
extern void  sound_register_fade_out(f64 volume, f64 target, int* handle_ptr, u32* position);
extern int   sound_is_position_in_range(f64 range, f32* position);
extern void  sound_init_play_params(int params_ptr);
extern u32   sound_play_request(int* params);
extern void  sound_stop_handle(void);
extern void  sound_set_pitch(f64 pitch, u32 handle);
extern void  sound_set_volume(f64 volume, u32 handle);
extern void  sound_set_pan(f64 pan, u32 handle);
extern void  sound_update_3d_volume(f64 volume, u32 handle, u32* position);
extern u32   FUN_8013104c(void);               // Random int
extern void  memset_wrapper(int addr, int val, int size);
extern void* FUN_803826e0(void);
extern void* FUN_803826e8(void);
extern void* FUN_803826ec(void);
extern void* FUN_803826f0(void);
extern void  FUN_8038272c(void);
extern void  FUN_80382734(void);
extern void  FUN_80382738(void);
extern void  FUN_8038273c(void);

// 0x800108b8 | sound_recycle_looping_slot
// Finds the lowest-priority looping slot (oldest or empty), fades it out, returns pointer
u32* sound_recycle_looping_slot(void) {
    FUN_803826e8();  // Disable interrupts

    f64 min_age = (f64)FLOAT_8061377c;
    u32* best_slot = DAT_804cdb20;

    for (int i = 0; i < 6; i++) {
        int offset = i * 0xC;
        u32* slot = (u32*)((int)DAT_804cdb20 + offset);

        if ((int)*slot < 0) {
            // Empty slot: best candidate
            min_age = (f64)FLOAT_80613780;
            best_slot = slot;
        } else {
            int playing = sound_is_playing(*slot);
            if (playing == 0) {
                // Stopped: reclaim
                min_age = (f64)FLOAT_80613780;
                *slot = 0xFFFFFFFF;
                best_slot = slot;
            } else {
                // Compare age/priority
                f32 age = *(f32*)((int)DAT_804cdb20 + offset + 8);
                if ((f64)age < min_age) {
                    best_slot = slot;
                    min_age = (f64)age;
                }
                // Decay age
                *(f32*)((int)DAT_804cdb20 + offset + 8) *= FLOAT_80613784;
            }
        }
    }

    // Fade out the slot we're recycling
    sound_register_fade_out((f64)best_slot[1], (f64)FLOAT_80613788, (int*)best_slot, NULL);

    FUN_80382734();  // Restore interrupts
    return best_slot;
}

// 0x80010994 | sound_play_ambient_3d
// Plays a randomized ambient sound at a 3D position if in range
void sound_play_ambient_3d(f64 range, f64 base_volume) {
    f32* position = (f32*)FUN_803826f0();
    f64 vol = range;  // volume from register

    if (!sound_is_position_in_range(vol, position)) {
        FUN_8038273c();
        return;
    }

    u32* slot = sound_recycle_looping_slot();

    // Randomize pitch
    u32 random_val = FUN_8013104c();
    f64 pitch = (f64)(FLOAT_80613790 *
                      ((f32)((f64)random_val - DOUBLE_80613798) / FLOAT_80613794) +
                      FLOAT_8061378c);

    // Set up play params
    int params[6];
    sound_init_play_params((int)params);
    // params[0] = sound_def_index (from register)
    // position, volume, pitch set in struct
    u32 handle = sound_play_request(params);

    *slot = handle;
    slot[2] = (u32)(f32)base_volume;  // age/priority
    slot[1] = (u32)(f32)base_volume;  // volume

    FUN_8038273c();
}

// 0x80010a94 | sound_init_persistent_slots
// Initializes persistent sound slot tracking (4 slots, stride 0x0C)
void sound_init_persistent_slots(void) {
    memset_wrapper((int)DAT_804cdb68 - 0x80000000, 0, 0x30);

    for (int i = 0; i < 4; i++) {
        int offset = i * 0xC;
        *(u32*)((int)DAT_804cdb68 + offset) = 0xFFFFFFFF;      // sound_id = invalid
        *(u32*)((int)DAT_804cdb68 + offset + 4) = 0xFFFFFFFF;  // handle = invalid
    }
}

// 0x80010af0 | sound_find_free_persistent_slot
// Finds an available persistent sound slot. Returns NULL if all full.
int* sound_find_free_persistent_slot(void) {
    for (int i = 0; i < 4; i++) {
        int offset = i * 0xC;
        if (*(int*)((int)DAT_804cdb68 + offset) == -1) {
            return (int*)((int)DAT_804cdb68 + offset);
        }
    }
    return NULL;
}

// 0x80010b24 | sound_assign_persistent_slot
// Writes a persistent slot entry: sound_id, handle, flags (has_position | is_looping)
void sound_assign_persistent_slot(u32* slot, u32 sound_id, int handle, int has_position, int is_looping) {
    if (handle < 0) return;

    slot[0] = sound_id;
    slot[1] = (u32)handle;
    u16 flags = (u16)(has_position != 0);
    flags |= (is_looping != 0) ? 2 : 0;
    *(u16*)(slot + 2) = flags;
}

// 0x80010b60 | sound_play_persistent
// Plays a persistent sound (looping or one-shot, 2D or 3D) and registers it
void sound_play_persistent(u32 sound_id, u32 position_ptr, int is_looping) {
    void* state = FUN_803826e8();
    int sound_def = (int)((u64)(u32)state >> 32);  // from register pair

    int* slot = sound_find_free_persistent_slot();
    if ((slot == NULL) || ((int)state < 0)) {
        FUN_80382734();
        return;
    }

    u32 flags = 0x20;  // one-shot flag
    if (is_looping != 0) {
        flags = 0x2A0;  // looping flag
    }

    u32 handle;
    if ((int)state == 0) {
        // 2D playback
        int params[6];
        sound_init_play_params((int)params);
        // Set flags, volume
        params[0] = sound_def;
        handle = sound_play_request(params);
    } else {
        // 3D playback with position
        int params[8];
        sound_init_play_params((int)params);
        // Set flags, volume, position, range
        params[0] = sound_def;
        handle = sound_play_request(params);
    }

    sound_assign_persistent_slot((u32*)slot, sound_def, handle, (int)state, is_looping);
    FUN_80382734();
}

// 0x80010c94 | sound_transfer_to_persistent
// Transfers an active sound handle to a persistent slot (for sounds that outlive their emitter)
void sound_transfer_to_persistent(int sample_id, u32* handle_ptr, int has_position) {
    FUN_803826ec();  // Disable interrupts

    int* slot = sound_find_free_persistent_slot();
    if (slot == NULL) {
        // No slot: force stop
        sound_stop_handle();
        *handle_ptr = 0xFFFFFFFF;
    } else {
        // Slow down to default pitch and register
        sound_set_pitch((f64)FLOAT_806137ac, *handle_ptr);
        sound_assign_persistent_slot((u32*)slot, sample_id, *handle_ptr, has_position, 0);
        *handle_ptr = 0xFFFFFFFF;
    }

    FUN_80382738();  // Restore interrupts
}

// 0x80010d18 | sound_stop_persistent_by_id
// Stops all persistent sounds matching a given sound ID
void sound_stop_persistent_by_id(int sound_id, int stop_first_only) {
    FUN_803826e0();  // Disable interrupts

    if (sound_id >= 0) {
        for (int i = 0; i < 4; i++) {
            int offset = i * 0xC;
            if (sound_id == *(int*)((int)DAT_804cdb68 + offset)) {
                sound_stop_handle();
                *(int*)((int)DAT_804cdb68 + offset) = -1;
                if (stop_first_only) break;
            }
        }
    }

    FUN_8038272c();  // Restore interrupts
}

// 0x80010d9c | sound_stop_all_persistent
// Stops and clears all 4 persistent sound slots
void sound_stop_all_persistent(void) {
    FUN_803826e8();  // Disable interrupts

    for (int i = 0; i < 4; i++) {
        sound_stop_handle();
        *(u32*)((int)DAT_804cdb68 + i * 0xC) = 0xFFFFFFFF;
    }

    FUN_80382734();  // Restore interrupts
}

// 0x80010dfc | sound_play_or_resume_3d
// Plays a new 3D sound or resumes a previously-persistent one at the given position.
// Checks persistent slot table first — if a matching sound_id is found, reclaims
// that handle (updates pitch/volume/position) instead of starting a new one.
void sound_play_or_resume_3d(f64 range, f64 pitch, f64 volume,
                             int sound_id, f32* position, int is_looping) {
    FUN_803826e8();  // Disable interrupts

    f32 base_vol = FLOAT_806137ac;
    if (is_looping != 0) {
        base_vol = FLOAT_806137b0;
    }

    if (sound_id < 0) goto done;

    // Check if this sound is already in a persistent slot
    for (int i = 0; i < 4; i++) {
        int offset = i * 0xC;
        if (sound_id == *(int*)((int)DAT_804cdb68 + offset)) {
            u32* p_handle = (u32*)((int)DAT_804cdb68 + offset + 4);

            // Resume: update pitch, volume, position
            sound_set_pitch(pitch, *p_handle);
            if (position == NULL) {
                sound_update_3d_volume(volume, *p_handle, NULL);
            } else {
                sound_set_volume((f64)base_vol, *p_handle);
                sound_set_pan(range, *p_handle);
                sound_update_3d_volume(volume, *p_handle, (u32*)position);
            }

            // Remove from persistent table (now managed by caller)
            *(u32*)((int)DAT_804cdb68 + offset) = 0xFFFFFFFF;
            goto done;
        }
    }

    // Not in persistent table: play fresh
    u32 flags = is_looping ? 0x280 : 0;

    if (position == NULL) {
        // 2D
        int params[6];
        sound_init_play_params((int)params);
        params[0] = sound_id;
        // set volume, flags
        sound_play_request(params);
    } else {
        // 3D
        int params[8];
        sound_init_play_params((int)params);
        params[0] = sound_id;
        // set range, pitch, volume, position, flags
        sound_play_request(params);
    }

done:
    FUN_80382734();  // Restore interrupts
}
