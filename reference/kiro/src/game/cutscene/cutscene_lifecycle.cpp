// =============================================================================
// cutscene_lifecycle.cpp — Cutscene render dispatch and scene integration
// TimeSplitters: Future Perfect (GameCube)
// Batch 155 | Date: 2026-08-10
// =============================================================================

// External globals
extern int DAT_80611b68;     // Active cutscene data pointer
extern int DAT_80611b58;     // Frame buffer A (allocated per-render)
extern int DAT_80611b5c;     // Frame buffer B (allocated per-render)

// External functions
void* FUN_803826e8(void);
void* FUN_803826f0(void);
void FUN_8038273c(void);
void FUN_80382734(void);
void cutscene_set_frame_timestamps(int);
void cutscene_clear_frame_timestamps(int);
void cutscene_eval_skel_lighting(void);
void cutscene_eval_secondary_lighting(void);
int cutscene_frame_in_range(int, int);
int cutscene_mesh_is_visible(void);
void cutscene_entity_draw(int, int, int);
int cutscene_update_visibility_flag(int, int, int);
int cutscene_is_ready(void);
void cutscene_entity_render_dispatch(int);
void* mem_alloc(void*, void*, void*, void*, void*, void*, void*, void*, int, int, void*, int, int, int, int, int);
void mem_free_top(int);

// =============================================================================
// Address: 0x8006b354 | cutscene_entity_render_dispatch
// Per-slot render coordinator. Allocates frame buffers if the cutscene has
// multiple animation tracks, evaluates skeleton lighting, checks frame-range
// visibility, draws the entity, then evaluates secondary lighting for attached
// entities. Frees frame buffers on exit.
// =============================================================================
void cutscene_entity_render_dispatch(int slot_index)
{
    void* ctx = FUN_803826e8();
    int raw_index = (int)((unsigned long long)ctx >> 32);
    int slot_offset = raw_index * 0x2c;

    int render_state = *(int*)(DAT_80611b68 + slot_offset + 0x4c);
    int entity_ptr = *(int*)(DAT_80611b68 + slot_offset + 0x48);

    // Allocate per-frame scratch buffers if multiple anim tracks
    if (0 < *(int*)(DAT_80611b68 + 0x2c)) {
        DAT_80611b58 = (int)mem_alloc(0, 0, 0, 0, 0, 0, 0, 0, 16000, 0, 0, 0, 0, 0, 0, 0);
        DAT_80611b5c = (int)mem_alloc(0, 0, 0, 0, 0, 0, 0, 0, 32000, 0, 0, 0, 0, 0, 0, 0);
    }

    // Draw mesh-only slot
    if (render_state != 0) {
        int slot_base = DAT_80611b68 + slot_offset;
        float start_time = *(float*)(slot_base + 0x34);
        float end_time = *(float*)(slot_base + 0x38);
        float current_time = *(float*)(DAT_80611b68 + 8);

        if (start_time <= current_time && current_time < end_time &&
            (*(unsigned int*)(slot_base + 0x58) & 1) == 0) {

            cutscene_set_frame_timestamps(render_state);
            cutscene_eval_skel_lighting();

            if (cutscene_frame_in_range(DAT_80611b68, raw_index)) {
                if (cutscene_mesh_is_visible() != 0) {
                    cutscene_entity_draw(DAT_80611b68, raw_index, (int)ctx);
                }
            }

            cutscene_clear_frame_timestamps(render_state);
        }
    }

    // Evaluate secondary lighting for entity-backed slot
    if (entity_ptr != 0) {
        int vis = cutscene_update_visibility_flag(entity_ptr, DAT_80611b68, raw_index);
        if (vis != 0) {
            float start_time = *(float*)(DAT_80611b68 + slot_offset + 0x34);
            float end_time = *(float*)(DAT_80611b68 + slot_offset + 0x38);
            float current_time = *(float*)(DAT_80611b68 + 8);

            if (start_time <= current_time && current_time < end_time) {
                cutscene_eval_secondary_lighting();
            }
        }
    }

    // Free per-frame scratch buffers
    if (0 < *(int*)(DAT_80611b68 + 0x2c)) {
        mem_free_top(DAT_80611b5c);
        mem_free_top(DAT_80611b58);
    }
    DAT_80611b5c = 0;
    DAT_80611b58 = 0;

    FUN_80382734();
}

// =============================================================================
// Address: 0x8006b4d4 | cutscene_render_slot_callback
// Scene render callback for a single cutscene slot. Converts raw slot pointer
// to slot index and dispatches to cutscene_entity_render_dispatch.
// =============================================================================
void cutscene_render_slot_callback(int slot_ptr)
{
    if (cutscene_is_ready() != 0) {
        int slot_index = (slot_ptr - (DAT_80611b68 + 0x30)) / 0x2c;
        cutscene_entity_render_dispatch(slot_index);
    }
}

// =============================================================================
// Address: 0x8006b530 | cutscene_render_all_slots
// Scene render callback that draws ALL cutscene slots. Called from
// scene_render_dispatch when cutscene is the full-screen layer.
// =============================================================================
void cutscene_render_all_slots(void)
{
    if ((DAT_80611b68 == 0 || *(int*)(DAT_80611b68 + 0x20) != 0) &&
        cutscene_is_ready() != 0) {

        int slot_count = *(int*)(DAT_80611b68 + 0x24);
        for (int i = 0; i < slot_count; i++) {
            cutscene_entity_render_dispatch(i);
        }
    }
}

// =============================================================================
// Address: 0x8006b5ac | cutscene_lighting_pass
// Iterates all cutscene slots and evaluates secondary (attached entity)
// lighting for slots within their active time range. Called during the scene
// lighting phase.
// =============================================================================
void cutscene_lighting_pass(void)
{
    int ready_flag = (int)FUN_803826f0();

    if (DAT_80611b68 != 0) {
        int slot_count = *(int*)(DAT_80611b68 + 0x24);
        int slot_offset = 0;

        for (int i = 0; i < slot_count; i++) {
            int slot_base = DAT_80611b68 + slot_offset;
            int entity_ptr = *(int*)(slot_base + 0x48);

            if (entity_ptr != 0 && ready_flag != 0) {
                float start_time = *(float*)(slot_base + 0x34);
                float end_time = *(float*)(slot_base + 0x38);
                float current_time = *(float*)(DAT_80611b68 + 8);

                if (start_time <= current_time && current_time < end_time) {
                    cutscene_eval_secondary_lighting();
                }
            }

            slot_offset += 0x2c;
        }
    }

    FUN_8038273c();
}

// =============================================================================
// Address: 0x8006b64c | cutscene_load_and_start
// Loads and starts a cutscene. Shuts down any active cutscene, sets paths,
// checks availability, parses info file, loads scene data, initializes playback
// state. Returns 1=success, 0=not available.
// =============================================================================
int cutscene_load_and_start(int name_id, int pak_id)
{
    if (DAT_80611b68 != 0) {
        cutscene_shutdown();
    }

    cutscene_set_name_and_pak(name_id, pak_id);
    int available = cutscene_is_available();
    if (!available) {
        return 0;
    }

    cutscene_parse_info_file();
    stream_process_callbacks();
    DAT_80611974 = cutscene_load_scene_data();

    DAT_80611b68 = DAT_80611974;
    *(float*)(DAT_80611974 + 8) = FLOAT_80613f00;  // time = 0.0
    *(int*)(DAT_80611b68 + 4) = 0;

    // Mark animation tracks as active in global table
    int slot_count = *(int*)(DAT_80611b68 + 0x24);
    int table_offset = 0;
    for (int i = 0; i < slot_count; i++) {
        short track_idx = *(short*)((int)&DAT_804d1cda + table_offset);
        table_offset += 2;
        int entry_offset = track_idx * 8;
        *(unsigned int*)((int)&DAT_804d34dc + entry_offset) |= 0x804000;
    }

    return 1;
}

// =============================================================================
// Address: 0x8006b730 | cutscene_begin_playback
// Triggers actual playback after loading. Resets time to 0, stores vsync frame
// reference, clears hidden/streaming/skip flags, sets active=1.
// =============================================================================
void cutscene_begin_playback(void)
{
    *(float*)(DAT_80611b68 + 8) = FLOAT_80613f00;  // time = 0.0
    *(int*)(DAT_80611b68 + 4) = DAT_80612020;       // vsync base frame
    DAT_80611b84 = 0;   // hidden flag
    DAT_80611b60 = 0;   // streaming flag
    DAT_80611b7c = 0;   // skip counter
    DAT_80611b64 = 1;   // active flag
}

// =============================================================================
// Address: 0x8006b764 | cutscene_tick
// Main per-frame cutscene update. Detects frame-skip/load stalls, waits for
// streaming, advances time, updates slots + keyframes while playing, triggers
// shutdown + camera restore at end.
// =============================================================================
void cutscene_tick(void)
{
    if (cutscene_is_ready() == 0) {
        return;
    }

    // Frame-skip detection
    if (DAT_80611b84 == 0) {
        DAT_80611b7c++;
    }

    // Detect load stall (XOR heuristic with vsync counter)
    if (DAT_80611b84 == 0) {
        int xor_val = DAT_80611b7c ^ DAT_806109c0;
        int stall_check = (xor_val >> 1) - (xor_val & DAT_80611b7c);
        if (stall_check < 0) {
            FUN_80232494();
            DAT_80611b84 = 1;
        }
    }

    // Wait for streaming to complete
    if (DAT_80611b60 == 0) {
        DAT_80611b60 = FUN_802323fc();
    }

    if (DAT_80611b60 == 0) {
        // Not ready yet — reset time base
        *(int*)(DAT_80611b68 + 4) = DAT_80612020;
        *(float*)(DAT_80611b68 + 8) = FLOAT_80613f00;
    } else {
        // Advance playback time
        cutscene_advance_time(DAT_80611b68);
    }

    cutscene_empty_callback();
    FUN_80238914(DAT_80611b68);

    // Check if still within duration
    float duration = (float)cutscene_get_duration();
    float current_time = *(float*)(DAT_80611b68 + 8);

    if (current_time < duration) {
        // Still playing — update slots and keyframes
        cutscene_update_slots();
        cutscene_process_keyframe(DAT_80611b68);
    } else {
        // End of cutscene — shutdown and restore camera
        cutscene_shutdown();
        int camera_state = DAT_80611d74;
        float fov = (float)FUN_8009c278(DAT_80611d54);
        FUN_8009d860(FLOAT_80613f00, fov,
                     *(int*)(camera_state + 8),
                     DAT_80611d54 + 0x27c,
                     (float*)(camera_state + 0xc0));
    }
}

// =============================================================================
// Address: 0x8006b89c | cutscene_unload_entities
// Destroys all render entity instances in cutscene slots. Unloads textures,
// clears entity table entries, calls entity destructor, NULLs slot pointers.
// =============================================================================
void cutscene_unload_entities(void)
{
    FUN_803826ec();

    if ((DAT_805015c0 & 0x200) != 0 && DAT_80611b68 != 0) {
        int slot_offset = 0;
        int slot_count = *(unsigned int*)(DAT_80611b68 + 0x24);

        for (unsigned int i = 0; i < slot_count; i++) {
            int* entity_ptr = *(int**)(DAT_80611b68 + slot_offset + 0x48);

            if (entity_ptr != NULL) {
                // Resolve entity table entry
                int table_idx = *(short*)((int)&DAT_805c9858 +
                    (unsigned int)*(unsigned short*)(entity_ptr + 0x25) * 2);
                int table_entry = DAT_806125e8 + table_idx * 0x30;

                render_entity_unload_textures();
                *(int*)(table_entry + 4) = 0;
                FUN_800735e4(entity_ptr);
                *(int*)(DAT_80611b68 + slot_offset + 0x48) = 0;
            }

            slot_offset += 0x2c;
        }
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x8006b964 | cutscene_shutdown
// Full cutscene teardown: unload models, unload entities, reset all state,
// flush anim channels, clear active flag and paths.
// =============================================================================
void cutscene_shutdown(void)
{
    cutscene_unload_models();
    cutscene_unload_entities();
    cutscene_reset_all();
    FUN_80027e50();             // Flush animation channels
    DAT_80611b64 = 0;           // Active flag = 0
    cutscene_set_name_and_pak(0, 0);
    DAT_80611b68 = 0;           // Clear cutscene data pointer
}

// =============================================================================
// Address: 0x8006b9ac | cutscene_music_state_reset
// Resets music sync accumulator to 0. Called when starting a new cutscene or
// after music transitions.
// =============================================================================
int cutscene_music_state_reset(void)
{
    DAT_80611b70 = 0;
    return 0;
}

// =============================================================================
// Address: 0x8006b9bc | cutscene_music_tick
// Accumulates music_update_tick() result into music sync flag each frame.
// Allows cutscene system to detect when music playback has advanced.
// =============================================================================
void cutscene_music_tick(void)
{
    unsigned int tick_result = music_update_tick();
    DAT_80611b70 |= tick_result;
}

// =============================================================================
// Address: 0x8006b9e8 | cutscene_is_ready
// Returns non-zero if cutscene is loaded and ready for rendering.
// Checks: data pointer exists, system flags (0x20000200), music sync flag,
// and optional streaming completion.
// =============================================================================
int cutscene_is_ready(void)
{
    if (DAT_80611b68 == 0) {
        return 0;
    }

    if ((DAT_805015c0 & 0x20000200) == 0) {
        return 0;
    }

    int result = DAT_80611b70;

    if ((DAT_805015c0 & 0x200) != 0) {
        int streaming_done = FUN_80232364();
        if (streaming_done == 0 && DAT_80611b84 == 0) {
            result = 0;
        } else {
            result = 1;
        }
    }

    return result;
}

// =============================================================================
// Address: 0x8006ba78 | cutscene_speed_accumulate_check
// Accumulates cutscene playback speed. Returns 1 if over budget (speed >
// vsync_period * factor), 0 if OK to continue. Also checks music sync if
// stop was requested.
// =============================================================================
int cutscene_speed_accumulate_check(void)
{
    FLOAT_80611b80 += FLOAT_806108ec;

    float threshold = FLOAT_80613f54 *
        (float)((double)(unsigned int)DAT_806109c0 - DOUBLE_80613f20);

    if (FLOAT_80611b80 <= threshold ||
        (DAT_80610450 != 0 && music_update_tick() != 0)) {
        return 0;
    }

    return 1;
}

// =============================================================================
// Address: 0x8006baf8 | cutscene_bone_inverse_precompute
// Precomputes inverse bone matrices for cutscene entity. Allocates
// bone_count * 0x40 buffer and inverts each referenced bone matrix.
// =============================================================================
void cutscene_bone_inverse_precompute(void)
{
    int ctx = (int)FUN_803826e8();
    int entity_data = *(int*)(ctx + 4);

    // Determine bone count from animation data
    int bone_count = 0;
    if (*(int*)(entity_data + 0x80) != 0) {
        int anim_key = *(int*)(*(int*)(entity_data + 0x80) + 8);
        int track_entry = *(int*)(DAT_80611824 + (short)DAT_804d1cd0[anim_key] * 4);
        bone_count = *(int*)(track_entry + 0x14);
    }

    // Allocate and compute inverse matrices
    if (*(int*)(entity_data + 0x10) != 0 && *(int*)(entity_data + 8) != 0) {
        void* inv_buf = mem_advance_secondary(bone_count << 6);
        *(int*)(entity_data + 0x14) = (int)inv_buf;

        int dst_offset = 0;
        int idx_offset = 0;
        for (int i = 0; i < bone_count; i++) {
            int bone_idx = *(int*)(**(int**)(entity_data + 0x84) + idx_offset);
            float* src_mtx = (float*)(*(int*)(entity_data + 0x10) + bone_idx * 0x40);
            float* dst_mtx = (float*)(*(int*)(entity_data + 0x14) + dst_offset);
            FUN_80130b90(src_mtx, dst_mtx);
            dst_offset += 0x40;
            idx_offset += 4;
        }
    }

    FUN_80382734();
}
