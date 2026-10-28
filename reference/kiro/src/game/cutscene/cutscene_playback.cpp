// =============================================================================
// cutscene_playback.cpp — Cutscene system management and playback control
// TimeSplitters: Future Perfect (GameCube)
// Batch 154 | Date: 2026-08-10
// =============================================================================

// External globals
extern int DAT_80611b68;     // Active cutscene data pointer
extern int DAT_80611b78;
extern int DAT_80611b74;
extern int DAT_80611b84;     // Hidden flag
extern int DAT_80611b64;
extern int DAT_80610450;     // Frame counter
extern int DAT_80610454;     // Default frame rate
extern int DAT_80611974;
extern int DAT_80611970;
extern int DAT_80611930;     // Render disable flag
extern float FLOAT_80611b80; // Playback speed
extern float FLOAT_80613f00; // 0.0f / 1.0f constant
extern float FLOAT_8060faf8; // 30.0f (frame rate)
extern double DOUBLE_80613f20;

// External functions
void cutscene_system_init(void);
void level_actor_unload_all(void);
void FUN_80253eac(void*, double, double, void*, void*, void*, void*, void*);
void memset_wrapper(int, int, int);

// =============================================================================
// Address: 0x8006a848 | cutscene_get_data
// Returns pointer to the active cutscene data structure.
// Returns NULL (0) if no cutscene is playing.
// =============================================================================
int cutscene_get_data(void)
{
    return DAT_80611b68;
}

// =============================================================================
// Address: 0x8006a850 | cutscene_request_stop
// Marks the active cutscene for termination. Sets stop flag at +0x1C and
// clears the frame counter.
// =============================================================================
void cutscene_request_stop(void)
{
    if (DAT_80611b68 == 0) {
        return;
    }
    *(int*)(DAT_80611b68 + 0x1c) = 1;
    DAT_80610450 = 0;
}

// =============================================================================
// Address: 0x8006a870 | cutscene_get_duration
// Returns total cutscene duration in seconds: (frame_count - 1) / 30.0.
// Returns 0.0 if no cutscene is active.
// =============================================================================
float cutscene_get_duration(void)
{
    if (DAT_80611b68 == 0) {
        return FLOAT_80613f00;  // 0.0
    }

    // frame_count is first field of cutscene data
    int frame_count = *(int*)DAT_80611b68;
    float duration = (float)(frame_count - 1) / FLOAT_8060faf8;
    return duration;
}

// =============================================================================
// Address: 0x8006a8bc | cutscene_reset_all
// Full cutscene system reset. Zeroes 0x4B00-byte entity pool, clears all
// globals, sets default frame rate to 10, calls cutscene_system_init and
// level_actor_unload_all.
// =============================================================================
void cutscene_reset_all(void)
{
    memset_wrapper((int)0x80532a88, 0, 0x4b00);  // Clear 60-slot entity pool
    DAT_80611b78 = 0;
    DAT_80611b74 = 0;
    DAT_80611b68 = 0;
    DAT_80610454 = 10;     // Default frame rate
    DAT_80611974 = 0;
    DAT_80611970 = 0;
    cutscene_system_init();
    level_actor_unload_all();
}

// =============================================================================
// Address: 0x8006a914 | cutscene_system_startup
// Cutscene system startup entry point. Clears hidden flag, resets playback
// speed to 1.0, performs full reset, optionally initializes level audio.
// =============================================================================
void cutscene_system_startup(void)
{
    DAT_80611b84 = 0;              // Clear hidden flag
    FLOAT_80611b80 = FLOAT_80613f00;  // Speed = 1.0
    cutscene_reset_all();
    DAT_80611b64 = 0;

    if (DAT_80611930 == 0) {
        FUN_80253eac(0, 0.0, 0.0, 0, 0, 0, 0, 0);  // Init level audio
    }
}

// =============================================================================
// Address: 0x8006a95c | cutscene_init
// Convenience wrapper for cutscene_system_startup.
// =============================================================================
void cutscene_init(void)
{
    cutscene_system_startup();
}

// =============================================================================
// Address: 0x8006a97c | cutscene_slot_reset_render_state
// Resets texture and blend state indices for a cutscene slot to 0xFF
// (uninitialized), forcing re-evaluation on next draw.
// =============================================================================
void cutscene_slot_reset_render_state(int slot_ptr)
{
    *(unsigned char*)(slot_ptr + 0x13c) = 0xff;
    *(unsigned char*)(slot_ptr + 0x13d) = 0xff;
}

// =============================================================================
// Address: 0x8006a98c | cutscene_advance_time
// Advances cutscene playback time. Three timing modes:
//   0 = frame-locked (vsync counter difference)
//   1 = variable-rate (speed / vsync_period)
//   2 = fixed-rate (speed / FPS constant)
// Converts resulting time to frame index and calls anim_time_update.
// =============================================================================
void cutscene_advance_time(int cutscene_data)
{
    float current_time = *(float*)(cutscene_data + 8);

    // Check if time is past start threshold
    if (FLOAT_80613f40 < current_time || FLOAT_80613f00 < FLOAT_806108ec) {
        float duration = (float)cutscene_get_duration();
        float one = FLOAT_80613f2c;

        if (current_time < (one + duration) || FLOAT_806108ec < FLOAT_80613f00) {
            // Determine timing mode
            int mode = 0;
            if (DAT_80611778 != 0 || DAT_8061177c != 0) {
                mode = 2;
            }

            if (mode == 2) {
                // Fixed-rate: advance by speed/framerate
                *(float*)(cutscene_data + 8) += FLOAT_806108ec / FLOAT_8060faf8;
            } else if (mode == 1) {
                // Variable-rate: advance by speed/vsync_period
                float vsync_period = (float)((double)(unsigned int)DAT_806109c0 - DOUBLE_80613f20);
                *(float*)(cutscene_data + 8) += FLOAT_806108ec / vsync_period;
            } else {
                // Frame-locked: use vsync counter difference
                int synced = FUN_80130fc8();
                if (synced) {
                    float vsync_period = (float)((double)(unsigned int)DAT_806109c0 - DOUBLE_80613f20);
                    *(int*)(cutscene_data + 4) =
                        (int)-(current_time * vsync_period -
                               (float)((double)(unsigned int)DAT_80612020 - DOUBLE_80613f20));
                } else {
                    int counter_diff = DAT_80612020 - *(int*)(cutscene_data + 4);
                    float vsync_period = (float)((double)(unsigned int)DAT_806109c0 - DOUBLE_80613f20);
                    *(float*)(cutscene_data + 8) = (float)counter_diff / vsync_period;
                }
            }

            // Convert time to frame index
            *(int*)(cutscene_data + 0xc) =
                (int)(*(float*)(cutscene_data + 8) * FLOAT_8060faf8);

            FUN_80027cfc();
            return;
        }
    }

    // Clamp to duration
    float dur = (float)cutscene_get_duration();
    *(float*)(cutscene_data + 8) = dur;
    FUN_80027cfc();
}

// =============================================================================
// Address: 0x8006ab58 | cutscene_update_slots
// Per-frame cutscene slot update. Iterates all slots (stride 0x2C), initializes
// materials/timing on first activation, updates entity transforms, calls
// cutscene_update_visibility_flag. Handles mesh-only vs entity-backed slots.
// =============================================================================
void cutscene_update_slots(void)
{
    int anim_data = (int)FUN_803826e4();
    int slot_index = 0;
    int slot_offset = 0;

    while (slot_index < *(int*)(anim_data + 0x24)) {
        int slot_base = anim_data + slot_offset;
        int mesh_state = *(int*)(slot_base + 0x4c);
        int entity_ptr = *(int*)(slot_base + 0x48);

        if (mesh_state == 0 && entity_ptr == 0) {
            // Neither mesh nor entity — skip to visibility init
            goto check_first_activation;
        }

        if (mesh_state != 0) {
            cutscene_slot_reset_render_state(mesh_state);
        }

        // Check if time hasn't reached slot start yet
        if (*(float*)(anim_data + 8) < *(float*)(slot_base + 0x34) ||
            *(int*)(slot_base + 0x50) != 0) {
            goto check_first_activation;
        }

        // First-time activation
        if (entity_ptr == 0) {
            // Mesh-only slot: set animation index and timing
            *(int*)(mesh_state + 0x10) = *(int*)(slot_base + 0x30);
            *(int*)(mesh_state + 0x14) = (int)(short)DAT_804d1cd0[*(int*)(mesh_state + 0x10)];
            float vsync_period = (float)((double)(unsigned int)DAT_806109c0 - DOUBLE_80613f20);
            *(float*)(mesh_state + 0x18) =
                (*(float*)(anim_data + 8) - *(float*)(slot_base + 0x34)) -
                FLOAT_80613f2c / vsync_period;
            *(float*)(mesh_state + 0x44) = FLOAT_80613f2c;
            *(float*)(mesh_state + 0x68) = FLOAT_80613f00;
            *(float*)(mesh_state + 0x6c) = FLOAT_80613f00;
            *(float*)(mesh_state + 0x70) = FLOAT_80613f00;
            *(float*)(mesh_state + 0x74) = FLOAT_80613f00;
        } else {
            // Entity-backed slot: set material on render instance
            render_instance_set_material(*(int**)(entity_ptr + 4), *(int*)(slot_base + 0x30));
            *(float*)(*(int*)(entity_ptr + 4) + 0xd4) =
                *(float*)(anim_data + 8) - *(float*)(slot_base + 0x34);
        }
        *(int*)(slot_base + 0x50) = 1;
        goto update_entity;

check_first_activation:
        // Check if slot needs first-activation init
        if ((mesh_state != 0 || entity_ptr != 0) &&
            *(int*)(slot_base + 0x50) != 0 &&
            *(float*)(anim_data + 8) <= *(float*)(slot_base + 0x38)) {
            if (entity_ptr == 0) {
                *(float*)(mesh_state + 0x68) = FLOAT_80613f00;
                *(float*)(mesh_state + 0x6c) = FLOAT_80613f00;
                *(float*)(mesh_state + 0x70) = FLOAT_80613f00;
                *(float*)(mesh_state + 0x74) = FLOAT_80613f00;
            } else {
                FUN_80020908(anim_data, slot_index, *(int**)(entity_ptr + 4));
                *(int*)(entity_ptr + 0x34) = 0xd5;
                *(unsigned char*)(entity_ptr + 0x30) = 1;
                *(int*)(entity_ptr + 0x38) = *(int*)(entity_ptr + 0x34);
                *(float*)(entity_ptr + 100) = FLOAT_80613f00;
                *(float*)(entity_ptr + 0x68) = FLOAT_80613f00;
                *(float*)(entity_ptr + 0x6c) = FLOAT_80613f00;
                *(float*)(entity_ptr + 0x70) = FLOAT_80613f00;
                *(unsigned int*)(entity_ptr + 0x28) |= 0x40000000;
                *(unsigned int*)(entity_ptr + 0x18) |= 2;
            }
        }

update_entity:
        if (entity_ptr != 0) {
            FUN_8007170c(entity_ptr);
            cutscene_update_visibility_flag(entity_ptr, anim_data, slot_index);
        }

        slot_index++;
        slot_offset += 0x2c;
    }

    FUN_80382730();
}

// =============================================================================
// Address: 0x8006ad6c | cutscene_empty_callback
// Empty stub. No-op callback placeholder in cutscene dispatch table.
// =============================================================================
void cutscene_empty_callback(void)
{
    // intentionally empty
}

// =============================================================================
// Address: 0x8006ad70 | cutscene_playback_update
// MASSIVE main cutscene playback tick. Handles:
// - Loading cutscene data if not yet loaded
// - Music track selection and playback
// - Pause/resume on scene transitions
// - Time advancement with level-specific speed multipliers
// - Skip detection (hold button to fast-forward with countdown)
// - End-of-cutscene detection and stop request
// - Slot updates and keyframe processing
// =============================================================================
void cutscene_playback_update(void)
{
    void* ctx = FUN_803826f0();

    // Early exit if cutscene active but flagged as stopped
    if (DAT_80611b68 != NULL && ((int*)DAT_80611b68)[8] == 0) {
        FUN_8038273c();
        return;
    }

    // Load cutscene if not yet loaded
    if (DAT_80611b68 == NULL) {
        DAT_80611b68 = (int*)cutscene_load_if_needed(1);
        ((int*)DAT_80611b68)[2] = (int)FLOAT_80613f00;  // time = 0
        ((int*)DAT_80611b68)[1] = DAT_80612020;          // vsync base
        DAT_80610450 = 1;
        cutscene_music_state_reset();
    }

    // First-time music setup
    if (DAT_80611b74 == 0) {
        int track_id = cutscene_get_level_mapping((int)DAT_80501603);
        cutscene_load_if_needed(1);
        if (track_id >= 0) {
            music_play_track_id(track_id);
        }
        FUN_8009c2a0(9);
        DAT_80611b74 = 1;
    }

    // Music tick — sync vsync base if music not ready
    if (cutscene_music_tick() == 0) {
        ((int*)DAT_80611b68)[1] = DAT_80612020;
    }

    // Wait for readiness
    if (cutscene_is_ready() == 0) {
        FUN_8038273c();
        return;
    }

    // ... (speed multiplier selection based on DAT_80612614 level ID)
    // ... (time advancement via cutscene_advance_time)
    // ... (pause/resume handling with music_pause/resume)
    // ... (skip detection with button hold countdown)

    // End-of-cutscene check
    float duration = (float)cutscene_get_duration();
    if (duration <= (float)((int*)DAT_80611b68)[2]) {
        ((int*)DAT_80611b68)[2] = (int)duration;
        ((int*)DAT_80611b68)[3] = *((int*)DAT_80611b68) - 2;
        cutscene_request_stop();
    }

    // Per-frame slot and keyframe processing
    cutscene_update_slots();
    cutscene_process_keyframe((int)DAT_80611b68);

    // Early-end check (skip threshold)
    if (((int*)DAT_80611b68)[7] == 0) {
        float end_offset = (float)((int*)DAT_80611b68)[5];
        if ((float)(duration - end_offset) <= (float)((int*)DAT_80611b68)[2]) {
            cutscene_request_stop();
        }
    }

    FUN_8038273c();
}
