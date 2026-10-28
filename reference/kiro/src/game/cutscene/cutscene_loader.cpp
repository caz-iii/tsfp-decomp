// =============================================================================
// cutscene_loader.cpp - Cutscene scene data loading and unloading
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 060 | Date: 2026-08-05
// =============================================================================

#include "common.h"
#include "cutscene.h"
#include "anim.h"
#include "asset.h"
#include "memory.h"

// Forward declarations
extern void FUN_803826c4(void);
extern void FUN_803826dc(void);
extern void FUN_80382710(void);
extern void FUN_80382728(void);
extern void FUN_8012e77c(void* file_state, char* token_buf, ...);
extern int  FUN_8012ea4c(char* token, void* delimiters, int* out_index);
extern int  FUN_8012e818(void* file_state, char* token_buf, ...);
extern int  FUN_8038b338(char* str_a, const char* str_b, int len);
extern int  FUN_8038b378(u32* str_a, u32* str_b);
extern int  FUN_80387fb8(int token_val, char* out_buf, const char* fmt, ...);
extern int  FUN_80068f98(void);    // model load
extern int* FUN_8006a06c(int model);  // get model node list
extern int  FUN_800691ac(int node_list, char* path, int first_load, ...);
extern void FUN_80061da0(float scale, int* anim_data, int anim_index);
extern int  FUN_80072d70(int sprite_idx, float* pos, float* scale, int p3, int p4);
extern void FUN_80021f78(char* path);  // pack file load
extern int  FUN_80180434(void);
extern void FUN_8017c454(int anim_handle);
extern void FUN_8017c358(void);

// Cutscene model array - up to 60 entries, stride 0x114 bytes
// Base: DAT_80516750
extern int   g_cutscene_model_types[];      // DAT_80516750 - type (1=skeletal, 2=prop)
extern int   g_cutscene_model_ptrs[];       // DAT_80516854 - model pointers
extern int   g_cutscene_model_anim_start[]; // DAT_8051685c - first animation index
extern u8    g_cutscene_model_flags[];      // DAT_80516860 - flags (bit0=loaded, bit1=prop_loaded)

extern int   g_cutscene_model_count;        // DAT_80611998
extern int   g_cutscene_loading;            // DAT_806117c0

// Cutscene track data
extern int   g_cutscene_frame_count;        // DAT_805153e8
extern float g_cutscene_speed;              // DAT_805153f0
extern int   g_cutscene_track_count;        // DAT_8051540c
extern int   g_cutscene_use_pack;           // DAT_80515410
extern int   g_cutscene_anim_count;         // DAT_80515414

// String constants
extern const char s_assets_ts_cutscenes[];  // "assets/ts/cutscenes/"
extern const char s_gamecube_output[];      // "/gamecube/output"
extern const char s_fmt_path[];             // "%s/%s/%s_%s"

// -----------------------------------------------------------------------------
// Address: 0x8003b2dc | Name: cutscene_load_scene_data
// -----------------------------------------------------------------------------
// Loads cutscene scene data from .c2n info files. Parses model entries
// (type 1 = skeletal, type 2 = prop), animation tracks, and camera data.
// Initializes cutscene model array (max 60 entries at DAT_80516750).
// References: assets/ts/cutscenes/
// -----------------------------------------------------------------------------
void cutscene_load_scene_data(void) {
    int file_state = 0;
    u32 frame_count = 0;
    char* scene_name;
    char token_buf[64];
    char path_buf[128];
    u8   info_data[516];
    int  model_idx;
    int  track_count = 0;
    int  track_data_offset = 0;
    int  track_struct_offset = 0;
    int  prop_count = 0;
    int  result;

    FUN_803826c4();  // enter critical section

    scene_name = (char*)cutscene_get_name();
    g_cutscene_model_count = 0;

    // Clear cutscene model array
    memset_wrapper((void*)&g_cutscene_model_types, 0, 0x40B0);

    // Initialize all model anim start indices to -1
    for (int i = 0; i < 60; i++) {
        g_cutscene_model_anim_start[i] = -1;  // stride 0x114
    }

    g_cutscene_loading = 1;

    // Check if cutscene info file exists
    result = cutscene_info_file_exists();
    cutscene_build_info_path(info_data);
    asset_get_size(info_data);

    file_state = result;
    cutscene_system_init();

    // Initialize track defaults
    g_cutscene_frame_count = 0;
    g_cutscene_speed = 1.0f;  // FLOAT_80613b50
    // DAT_805153f8-80515400 = 0.0f (FLOAT_80613b5c)
    // DAT_80515404 = 0
    // DAT_80515408 = 1
    // DAT_8051540c = 0
    // DAT_80515410 = 0

    // Parse scene info file - first pass: load models and animations
    while (FUN_8012e77c(&file_state, token_buf, /*...*/) >= 0) {
        // Check for "FRAMES" keyword
        if (FUN_8038b338(token_buf, "FRAMES", 6) == 0) {
            frame_count = FUN_8012e818(&file_state, token_buf, /*...*/);
        }
        // Check for "MODEL" keyword (type 1 - skeletal)
        else if (FUN_8038b338(token_buf, "MODEL", 5) == 0) {
            // Read model name, build path, load model
            model_idx = g_cutscene_model_count;
            g_cutscene_model_types[model_idx] = 1;  // skeletal

            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);

            // Build asset path: assets/ts/cutscenes/<name>/gamecube/output/<model>.gcr
            FUN_80387fb8(/*...*/);

            int model_ptr = FUN_80068f98();  // load model
            g_cutscene_model_ptrs[model_idx] = model_ptr;

            int* node_list = FUN_8006a06c(0);
            // Store node list pointer in track data

            // Read animation name
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);

            g_cutscene_anim_count++;
            g_cutscene_model_count++;
        }
        // Check for "PROP" keyword (type 2 - prop/static)
        else if (FUN_8038b338(token_buf, "PROP", 4) == 0) {
            model_idx = g_cutscene_model_count;
            g_cutscene_model_types[model_idx] = 2;  // prop

            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);

            g_cutscene_model_count++;
        }
        // Check for "ANIM" keyword - animation track
        else if (FUN_8038b338(token_buf, "ANIM", 4) == 0) {
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);

            // Build animation asset path
            FUN_80387fb8(/*...*/);

            if (g_cutscene_use_pack == 0) {
                int anim_idx = anim_load_cutscene_asset(path_buf, track_count, /*...*/);
                // Store anim index in track
            } else {
                // Use pack-relative index
            }

            // Read model reference for this animation
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);

            // Match model name - try character first, then prop
            int match = cutscene_find_model_by_name(
                &g_cutscene_model_types, g_cutscene_model_count, token_buf);

            if (match != -1) {
                int match_offset = match * 0x114;

                if (g_cutscene_model_types[match] == 1) {
                    // Skeletal model - load animation onto model
                    int model = g_cutscene_model_ptrs[match];
                    int* nodes = FUN_8006a06c(model);

                    // Build full path and load animation
                    FUN_80387fb8(/*...*/);

                    if ((g_cutscene_model_flags[match] & 1) == 0) {
                        FUN_800691ac(nodes, path_buf, 1, /*...*/);
                        g_cutscene_model_flags[match] |= 1;
                    } else {
                        FUN_800691ac(nodes, path_buf, 0, /*...*/);
                    }

                    if (g_cutscene_model_anim_start[match] == -1) {
                        g_cutscene_model_anim_start[match] = track_count;
                    }
                }
                else if (g_cutscene_model_types[match] == 2) {
                    // Prop model - create sprite/entity
                    // ... prop setup code ...
                    if ((g_cutscene_model_flags[match] & 2) == 0) {
                        g_cutscene_model_flags[match] |= 2;
                        g_cutscene_model_anim_start[match] = track_count;
                    }

                    FUN_80061da0(1.0f, /*anim_data*/, /*anim_index*/);
                    prop_count++;
                }
            }

            FUN_8012e77c(&file_state, token_buf, /*...*/);
            track_count++;
            track_data_offset += 4;
            track_struct_offset += 0x2C;
        }
        // Check for "PACK" keyword
        else if (FUN_8038b338(token_buf, "PACK", 4) == 0) {
            // Read pack file path and load it
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012ea4c(token_buf, /*delimiters*/);
            FUN_80387fb8(/*...*/);

            int pack_ptr = pack_get_asset_ptr(path_buf, NULL);
            if (pack_ptr != 0) {
                g_cutscene_use_pack = 1;
                FUN_80021f78(path_buf);
            }
        }
    }

    // Second pass: read track timing/target data
    file_state = result;
    track_count = 0;
    track_struct_offset = 0;

    while (FUN_8012e77c(&file_state, token_buf, /*...*/) >= 0) {
        if (FUN_8038b338(token_buf, "MODEL", 5) == 0) {
            track_count++;
            track_struct_offset += 0x2C;
        }
        else if (FUN_8038b338(token_buf, "ANIM", 4) == 0) {
            // Skip 3 tokens (name, timing data)
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012e77c(&file_state, token_buf, /*...*/);
            FUN_8012e77c(&file_state, token_buf, /*...*/);

            // Check for "NONE" target
            if (FUN_8038b338(token_buf, "NONE", 4) == 0) {
                // g_track_targets[track] = -1
            } else {
                // Build model path and find matching model
                FUN_80387fb8(/*...*/);
                int target = cutscene_find_model_by_name(
                    &g_cutscene_model_types, g_cutscene_model_count, path_buf);
                if (target == -1) {
                    // Try prop path
                    FUN_80387fb8(/*...*/);
                    target = cutscene_find_model_by_name(
                        &g_cutscene_model_types, g_cutscene_model_count, path_buf);
                }
                if (target == -1) {
                    target = 0;
                }
                // Store target model's anim start
            }

            track_count++;
            track_struct_offset += 0x2C;
        }
    }

    // Finalize: compute per-track time offsets
    g_cutscene_frame_count = frame_count;
    g_cutscene_track_count = track_count;

    if (track_count > 0) {
        for (int i = 0; i < track_count; i++) {
            // Convert frame offset to normalized time
            // g_track_time[i] = (float)(frame_count ^ 0x80000000) / fps_constant
        }
    }

    g_cutscene_loading = 0;
    FUN_80382710();  // leave critical section
}

// -----------------------------------------------------------------------------
// Address: 0x8003bb64 | Name: cutscene_load_if_needed
// -----------------------------------------------------------------------------
// Guard wrapper for cutscene_load_scene_data. Only loads if not already loaded
// (DAT_80611974 == 0) and param_9 != 0. Sets loading flag during load.
// -----------------------------------------------------------------------------
static int g_cutscene_loaded = 0;    // DAT_80611974
static int g_cutscene_loading_flag = 0; // DAT_80611970

int cutscene_load_if_needed(int should_load) {
    if (g_cutscene_loaded == 0 && should_load != 0) {
        g_cutscene_loading_flag = 1;
        g_cutscene_loaded = (int)cutscene_load_scene_data();
        g_cutscene_loading_flag = 0;
    }
    return g_cutscene_loaded;
}

// -----------------------------------------------------------------------------
// Address: 0x8003bbb0 | Name: cutscene_unload_models
// -----------------------------------------------------------------------------
// Unloads all cutscene models. Iterates the 60-entry model array in reverse.
// For type-1 (skeletal) models: releases animation resources via FUN_8017c454,
// calls FUN_8017c358, and frees top memory.
// -----------------------------------------------------------------------------
void cutscene_unload_models(void) {
    FUN_803826dc();  // enter critical section

    // Iterate models in reverse (index 59 down to 0)
    for (int i = 59; i >= 0; i--) {
        int offset = i * 0x114;

        if (g_cutscene_model_anim_start[i] == -1) {
            // Already unloaded or never loaded
            g_cutscene_model_anim_start[i] = -1;
            continue;
        }

        int model_ptr = g_cutscene_model_ptrs[i];
        if (model_ptr == 0) {
            g_cutscene_model_anim_start[i] = -1;
            continue;
        }

        int* anim_list = *(int**)(model_ptr + 0x24);
        if (anim_list == NULL) {
            g_cutscene_model_anim_start[i] = -1;
            continue;
        }

        if (g_cutscene_model_types[i] != 1) {
            g_cutscene_model_anim_start[i] = -1;
            continue;
        }

        // Count animations (terminated by -1)
        int anim_count = 0;
        while (anim_list[anim_count * 4] != -1) {
            anim_count++;
        }

        // Release each animation handle
        int idx = 0;
        while (anim_list[idx * 4] != -1) {
            FUN_8017c454(anim_list[idx * 4]);
            anim_list[idx * 4] = 0x40000000;  // mark as released
            idx++;
        }

        FUN_8017c358();  // flush animation system

        // Free the memory allocated at model+0x2C
        mem_free_top(*(int*)(model_ptr + 0x2C));

        g_cutscene_model_anim_start[i] = -1;
    }

    FUN_80382728();  // leave critical section
}
