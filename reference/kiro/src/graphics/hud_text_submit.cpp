// ============================================================================
// HUD Text Submission & Drawing System
// Core text element pool management, submission by type, centered drawing,
// and per-player slot management.
// Batch 326 | Date: 2026-08-25
// ============================================================================

#include "common.h"
#include "graphics.h"
#include "text.h"
#include "sound.h"

// ---- HUD Text Element Layout (0x16C bytes per slot) ----
// +0x000: type (int) - element type (0=inactive, 1-13=active types)
// +0x004: text[0x140] - text buffer (320 chars)
// +0x144: time_current (float) - current animation time
// +0x148: time_fade_start (float) - when fade begins
// +0x14C: time_fade_end (float) - when element disappears
// +0x150: scale_y (float)
// +0x154: scale_x (float)
// +0x158: priority (int)
// +0x15C: color (int)
// +0x160: highlight (int)
// +0x164: line_count (int)
// +0x168: owner_id (int)

// ---- Pool Structure ----
// Per-player: 0x177C stride (DAT_805786b0 = write index, DAT_805786b8 = slot base)
// 16 slots per player, circular write pointer

#define HUD_TEXT_MAX_SLOTS 16
#define HUD_TEXT_SLOT_SIZE 0x16C
#define HUD_TEXT_BUF_SIZE  0x140
#define PLAYER_STRIDE      0x177C

// Type definitions
#define HUD_TYPE_INACTIVE   0
#define HUD_TYPE_MESSAGE    1
#define HUD_TYPE_SUBTITLE   2
#define HUD_TYPE_AWARD      3
#define HUD_TYPE_PERSIST_A  4
#define HUD_TYPE_PERSIST_B  5
#define HUD_TYPE_COUNTDOWN  6
#define HUD_TYPE_TYPEWRITER 7
#define HUD_TYPE_TIMED      8
#define HUD_TYPE_MULTILINE  9
#define HUD_TYPE_PICKUP    10
#define HUD_TYPE_SPECIAL_A 11
#define HUD_TYPE_OBJECTIVE 12
#define HUD_TYPE_PERSIST_C 13

// ============================================================================
// Address: 0x80105d5c | hud_text_submit
// MASSIVE core HUD text submission function.
// Allocates slot from 16-entry circular buffer per player.
// Copies text (0x140 bytes), sets type-specific timing/scale/fade params.
// Deduplicates: if same type+text already exists (except type 6), skips.
// Type switch sets: duration, fade speed, scale, line count.
// ============================================================================
int hud_text_submit(int player_idx, int type, u32* text_buf) {
    long long context = gx_begin_text_pass();  // FUN_803826ec
    int player = (int)(context >> 32);
    int y_offset = (int)context;

    int pool_offset = player * PLAYER_STRIDE;

    // Validate inputs
    if (player < 0 || player >= 4 || text_buf == NULL || type <= 0 || type >= 14) {
        goto done;
    }

    // Deduplication check (skip for type 6 countdown)
    if (type != HUD_TYPE_COUNTDOWN) {
        int search_idx = HUD_TEXT_MAX_SLOTS - 1;
        int write_pos = g_hud_write_index[pool_offset];
        for (int i = 0; i < HUD_TEXT_MAX_SLOTS; i++) {
            int wrapped = (write_pos + search_idx) & 0xF;
            int* slot = &g_hud_text_pool[player * 0x5DF + wrapped * 0x5B + 0xE];
            if (slot[0] == type) {
                if (strcmp((char*)(slot + 1), (char*)text_buf) == 0) {
                    goto done;  // Already showing this text
                }
                break;
            }
            search_idx--;
        }
    }

    // Allocate next slot (circular)
    int idx = g_hud_write_index[pool_offset];
    g_hud_write_index[pool_offset] = idx + 1;
    int* slot = &g_hud_text_pool[player * 0x5DF + idx * 0x5B + 0xE];
    if (g_hud_write_index[pool_offset] > 15) {
        g_hud_write_index[pool_offset] = 0;
    }

    // Copy text and initialize common fields
    memcpy(slot + 1, text_buf, HUD_TEXT_BUF_SIZE);
    slot[0] = type;
    slot[0x59] = 0;                      // frame counter
    *((u8*)slot + 0x143) = 0;            // flags
    *(float*)(slot + 0x51) = -(float)y_offset;  // Y position (negated)
    slot[0x56] = 1;                      // active
    slot[0x57] = -1;                     // no highlight
    slot[0x5A] = -1;                     // no owner
    slot[0x58] = -1;                     // no link

    // Type-specific parameters
    switch (type) {
        case HUD_TYPE_MESSAGE:  // 1 - standard message
            *(float*)(slot + 0x52) = 1.0f;     // duration
            *(float*)(slot + 0x54) = 0.01f;    // fade speed
            *(float*)(slot + 0x55) = 1.0f;     // scale
            break;

        case HUD_TYPE_SUBTITLE:  // 2 - subtitle (multi-line aware)
            *(float*)(slot + 0x52) = 0.5f;     // short duration
            *(float*)(slot + 0x54) = 0.1f;     // faster fade
            *(float*)(slot + 0x55) = 0.01f;    // small scale
            slot[0x56] = (int)text_count_lines(0.0f, (char*)(slot + 1));
            break;

        case HUD_TYPE_AWARD:  // 3 - award popup
            *(float*)(slot + 0x52) = 0.01f;
            *(float*)(slot + 0x53) = 0.02f;    // fade start offset
            *(float*)(slot + 0x54) = 0.3f;
            *(float*)(slot + 0x55) = *(float*)(slot + 0x54);
            break;

        case HUD_TYPE_PERSIST_A:  // 4, 11 - persistent display A
        case HUD_TYPE_SPECIAL_A:
            *(float*)(slot + 0x52) = 0.5f;
            *(float*)(slot + 0x53) = 0.75f;
            *(float*)(slot + 0x54) = 1.0f;
            *(float*)(slot + 0x55) = *(float*)(slot + 0x54);
            break;

        case HUD_TYPE_PERSIST_B:  // 5, 13 - persistent display B
        case HUD_TYPE_PERSIST_C:
            *(float*)(slot + 0x52) = 0.333f;
            *(float*)(slot + 0x53) = 0.4f;
            *(float*)(slot + 0x54) = 1.0f;
            *(float*)(slot + 0x55) = *(float*)(slot + 0x54);
            break;

        case HUD_TYPE_TYPEWRITER:  // 7 - typewriter reveal
        {
            u32 len = text_strlen((char*)(slot + 1));
            *(float*)(slot + 0x52) = 0.3f + (float)len / 20.0f;  // duration scales with length
            *(float*)(slot + 0x54) = 1.0f + *(float*)(slot + 0x52);
            *(float*)(slot + 0x55) = *(float*)(slot + 0x54);
            g_typewriter_char_index[pool_offset] = 0;
            break;
        }

        case HUD_TYPE_TIMED:  // 8 - timed display
            *(float*)(slot + 0x52) = 0.2f;
            *(float*)(slot + 0x54) = 0.2f;
            *(float*)(slot + 0x55) = *(float*)(slot + 0x54);
            break;

        case HUD_TYPE_MULTILINE:  // 9 - multiline (length-proportional)
        {
            int len = strlen((char*)text_buf);
            float extra = 0.05f * (float)(len - 20);
            if (extra < 0.0f) extra = 0.0f;
            float base = 0.01f + extra;
            *(float*)(slot + 0x52) = base;
            *(float*)(slot + 0x54) = base;
            *(float*)(slot + 0x55) = 1.0f + extra;
            break;
        }
    }

done:
    gx_end_text_pass();  // FUN_80382738
    return (int)slot;
}

// ============================================================================
// Address: 0x8010606c | hud_text_alloc_slot
// Allocates next HUD text slot from per-player circular buffer.
// 16 slots per player. Returns pointer to slot, or NULL if invalid player.
// ============================================================================
int* hud_text_alloc_slot(int player_idx) {
    if (player_idx < 0 || player_idx >= 4) {
        return NULL;
    }

    int pool_offset = player_idx * PLAYER_STRIDE;
    int idx = g_hud_write_index[pool_offset];
    g_hud_write_index[pool_offset] = idx + 1;

    if (g_hud_write_index[pool_offset] >= HUD_TEXT_MAX_SLOTS) {
        g_hud_write_index[pool_offset] = 0;
    }

    return &g_hud_text_pool[idx * 0x5B + player_idx * 0x5DF];
}

// ============================================================================
// Address: 0x801060c4 | hud_text_submit_countdown
// Submits a countdown timer HUD element (type 6).
// Plays countdown sound (0xD6C) with volume based on game time scale.
// Skips if challenge mode with timer on player 1 in 2-player split.
// ============================================================================
void hud_text_submit_countdown(void) {
    long long context = gx_begin_overlay();  // FUN_803826f0
    int player = (int)(context >> 32);
    int pool_offset = player * PLAYER_STRIDE;
    float y_offset = -(float)(context & 0xFFFFFFFF);

    // Skip condition: challenge mode, screen mode 2, player 1, timer active
    int mode = get_screen_mode();
    if (mode == 2 && player == 1 && game_get_round_timer() != 0 &&
        g_game_sub_mode == 0x0A) {
        goto done;
    }

    if (player >= 0 && player < 4 && (int)context != 0) {
        int* slot = hud_text_alloc_slot(player);

        // Clear text
        *((char*)(slot + 1)) = '\0';
        slot[0x59] = (int)context;  // store context ref

        // Set type and timing
        slot[0] = HUD_TYPE_COUNTDOWN;
        *(float*)(slot + 0x51) = y_offset;
        *(float*)(slot + 0x52) = 0.3f;     // duration
        *(float*)(slot + 0x54) = 0.5f;     // fade
        *(float*)(slot + 0x55) = 0.6f;     // scale target
        slot[0x56] = 1;

        // Play countdown sound
        SoundPlayParams params;
        sound_init_play_params(&params);
        params.sound_id = 0xD6C;
        params.volume = (int)(y_offset * (float)g_time_scale);
        sound_play_request(&params);
    }

done:
    gx_end_overlay();  // FUN_8038273c
}

// ============================================================================
// Address: 0x8010621c | hud_text_submit_pickup
// Submits a pickup notification HUD element (type 10).
// Copies text, sets standard fade/scale, stores owner_id.
// ============================================================================
void hud_text_submit_pickup(int text, int unused, int owner_id) {
    long long context = gx_begin_overlay();
    float y_offset = -(float)(context & 0xFFFFFFFF);

    int* slot = hud_text_alloc_slot((int)(context >> 32));
    if (slot != NULL) {
        memcpy(slot + 1, (void*)context, HUD_TEXT_BUF_SIZE);
        slot[0] = HUD_TYPE_PICKUP;         // type 10
        *(float*)(slot + 0x51) = y_offset;
        *(float*)(slot + 0x52) = 0.01f;    // duration
        *(float*)(slot + 0x54) = 0.1f;     // fade
        *(float*)(slot + 0x55) = 0.01f;    // scale
        slot[0x56] = 1;
        slot[0x5A] = owner_id;             // owner
    }

    gx_end_overlay();
}

// ============================================================================
// Address: 0x801062b0 | hud_text_submit_objective
// Submits an objective notification HUD element (type 12).
// Identical structure to pickup but with type 12.
// ============================================================================
void hud_text_submit_objective(int text, int unused, int owner_id) {
    long long context = gx_begin_overlay();
    float y_offset = -(float)(context & 0xFFFFFFFF);

    int* slot = hud_text_alloc_slot((int)(context >> 32));
    if (slot != NULL) {
        memcpy(slot + 1, (void*)context, HUD_TEXT_BUF_SIZE);
        slot[0] = HUD_TYPE_OBJECTIVE;      // type 12
        *(float*)(slot + 0x51) = y_offset;
        *(float*)(slot + 0x52) = 0.01f;
        *(float*)(slot + 0x54) = 0.1f;
        *(float*)(slot + 0x55) = 0.01f;
        slot[0x56] = 1;
        slot[0x5A] = owner_id;
    }

    gx_end_overlay();
}

// ============================================================================
// Address: 0x80106344 | hud_text_clear_by_type
// Clears HUD text slots matching a specific type for a given player.
// If type == -1, clears all except persistent types (4, 5, 13).
// ============================================================================
void hud_text_clear_by_type(int player_idx, int type) {
    if (player_idx < 0 || player_idx > 3) {
        return;
    }

    int base = player_idx * PLAYER_STRIDE;
    for (int i = 0; i < HUD_TEXT_MAX_SLOTS; i++) {
        int* slot_type = (int*)(&g_hud_text_pool + base + i * HUD_TEXT_SLOT_SIZE + 0x38);
        int current_type = *slot_type;

        if (current_type == type ||
            (type == -1 && current_type != HUD_TYPE_PERSIST_A &&
             current_type != HUD_TYPE_PERSIST_B &&
             current_type != HUD_TYPE_PERSIST_C)) {
            *slot_type = HUD_TYPE_INACTIVE;
        }
    }
}

// ============================================================================
// Address: 0x801063b4 | hud_text_draw_centered
// Draws centered HUD text with shadow on the primary viewport.
// Measures text, centers horizontally, computes fade alpha from element
// timing fields (current_time vs fade_start/fade_end).
// Uses special layout colors (grey 0x7F7F7F7F) or normal (0x4069697F/0x507F).
// ============================================================================
void hud_text_draw_centered(void) {
    int* element = gx_get_current_element();  // FUN_803826e4

    u32 color_main  = 0x4069697F;
    u32 color_shadow = 0x507F;

    if (hud_is_special_layout()) {
        color_main  = 0x40;
        color_shadow = 0x7F7F7F7F;
        set_render_layer(1);
    } else {
        set_render_layer(2);
    }

    hud_select_font_for_width();

    // Measure text dimensions
    int text_width, text_height;
    text_get_dimensions_int(&text_width, &text_height, (u8*)(element + 1));

    // Center horizontally in viewport
    int vp_left = g_viewport_offset_left + g_viewport_data[0];
    int vp_right = g_viewport_data[1] - g_viewport_offset_right;
    int x_pos = vp_left + ((vp_right - vp_left) >> 1) - (text_width >> 1);

    int y_pos = hud_get_center_y();

    // Compute fade alpha from timing
    float current_time = *(float*)(element + 0x144/4);
    float fade_end     = *(float*)(element + 0x14C/4);
    float fade_start   = *(float*)(element + 0x148/4);
    double alpha;

    if (fade_end <= current_time) {
        float past_end = fade_end + fade_start;
        if (current_time < past_end) {
            alpha = (double)0.02f;  // FLOAT_80615734 = very small
        } else {
            alpha = (double)(0.02f * ((current_time - past_end) / fade_end) + 0.02f);
        }
    } else {
        alpha = (double)(0.02f * (current_time / fade_end));
    }

    // Draw text twice (left-justified sweep effect)
    float draw_x = compute_draw_x(x_pos, alpha, vp_right - vp_left);
    text_draw_char_shadowed(draw_x, (float)y_pos, element + 1, color_main, color_shadow);

    float draw_x_mirror = compute_draw_x_mirror(x_pos, alpha, vp_right - vp_left);
    text_draw_char_shadowed(draw_x_mirror, (float)y_pos, element + 1, color_main, color_shadow);

    gx_end_element_pass();  // FUN_80382730
}

// ============================================================================
// Address: 0x8010662c | hud_text_draw_centered_mirror
// Draws centered HUD text on a mirror/secondary viewport.
// Sets up temporary full-screen viewport, draws identically to
// hud_text_draw_centered, then restores original viewport/camera.
// ============================================================================
void hud_text_draw_centered_mirror(void) {
    int* element = gx_get_current_element();

    int mirror_viewport = get_mirror_viewport();  // FUN_80229ee8
    int saved_camera = g_active_camera;
    int* saved_viewport = g_viewport_data;

    if (mirror_viewport != 0) {
        g_active_camera = mirror_viewport;
        setup_fullscreen_viewport(g_temp_viewport, 0, 0,
                                  g_screen_width - 1, g_screen_height - 1);
        g_viewport_data = g_temp_viewport;

        hud_select_font_for_width();
        set_render_layer(2);

        int text_width, text_height;
        text_get_dimensions_int(&text_width, &text_height, (u8*)(element + 1));

        int vp_left = g_viewport_offset_left + g_viewport_data[0];
        int vp_right = g_viewport_data[1] - g_viewport_offset_right;
        int x_pos = vp_left + ((vp_right - vp_left) >> 1) - (text_width >> 1);

        int y_pos = hud_get_center_y();

        // Compute fade (same as hud_text_draw_centered)
        float current_time = *(float*)(element + 0x144/4);
        float fade_end     = *(float*)(element + 0x14C/4);
        float fade_start   = *(float*)(element + 0x148/4);
        double alpha;

        if (fade_end <= current_time) {
            float past_end = fade_end + fade_start;
            if (current_time < past_end) {
                alpha = (double)0.02f;
            } else {
                alpha = (double)(0.02f * ((current_time - past_end) / fade_end) + 0.02f);
            }
        } else {
            alpha = (double)(0.02f * (current_time / fade_end));
        }

        float draw_x = compute_draw_x(x_pos, alpha, vp_right - vp_left);
        text_draw_char_shadowed(draw_x, (float)y_pos, element + 1, 0x4069697F, 0x507F);

        float draw_x_mirror = compute_draw_x_mirror(x_pos, alpha, vp_right - vp_left);
        text_draw_char_shadowed(draw_x_mirror, (float)y_pos, element + 1, 0x4069697F, 0x507F);
    }

    g_viewport_data = saved_viewport;
    g_active_camera = saved_camera;
    gx_end_element_pass();
}

// ============================================================================
// Address: 0x801068c4 | hud_text_draw_centered_dual
// Draws centered HUD text on both primary and mirror viewports.
// Same as hud_text_draw_centered but also handles mirror viewport if present.
// Supports special layout color override.
// ============================================================================
void hud_text_draw_centered_dual(void) {
    int* element = gx_get_current_element_b();  // FUN_803826d8

    int mirror_viewport = get_mirror_viewport();

    u32 color_main  = 0x4069697F;
    u32 color_shadow = 0x507F;

    if (hud_is_special_layout()) {
        color_main  = 0x40;
        color_shadow = 0x7F7F7F7F;
        set_render_layer(1);
    } else {
        set_render_layer(2);
    }

    int saved_camera = g_active_camera;
    int* saved_viewport = g_viewport_data;
    int restore_camera = 0;
    int* restore_viewport = NULL;

    if (mirror_viewport != 0) {
        g_active_camera = mirror_viewport;
        setup_fullscreen_viewport(g_temp_viewport, 0, 0,
                                  g_screen_width - 1, g_screen_height - 1);
        g_viewport_data = g_temp_viewport;
        restore_camera = saved_camera;
        restore_viewport = saved_viewport;
    }

    hud_select_font_for_width();

    int text_width, text_height;
    text_get_dimensions_int(&text_width, &text_height, (u8*)(element + 1));

    int vp_left = g_viewport_offset_left + g_viewport_data[0];
    int vp_right = g_viewport_data[1] - g_viewport_offset_right;
    int x_pos = vp_left + ((vp_right - vp_left) >> 1) - (text_width >> 1);
    int y_pos = hud_get_center_y();

    // Fade computation (identical to hud_text_draw_centered)
    float current_time = *(float*)(element + 0x144/4);
    float fade_end     = *(float*)(element + 0x14C/4);
    float fade_start   = *(float*)(element + 0x148/4);
    double alpha;

    if (fade_end <= current_time) {
        float past_end = fade_end + fade_start;
        if (current_time < past_end) {
            alpha = (double)0.02f;
        } else {
            alpha = (double)(0.02f * ((current_time - past_end) / fade_end) + 0.02f);
        }
    } else {
        alpha = (double)(0.02f * (current_time / fade_end));
    }

    float draw_x = compute_draw_x(x_pos, alpha, vp_right - vp_left);
    text_draw_char_shadowed(draw_x, (float)y_pos, element + 1, color_main, color_shadow);

    float draw_x_mirror = compute_draw_x_mirror(x_pos, alpha, vp_right - vp_left);
    text_draw_char_shadowed(draw_x_mirror, (float)y_pos, element + 1, color_main, color_shadow);

    // Restore if mirror was active
    if (mirror_viewport != 0) {
        g_viewport_data = restore_viewport;
        g_active_camera = restore_camera;
    }

    gx_end_element_pass_b();  // FUN_80382724
}

// ============================================================================
// Address: 0x80106b98 | hud_text_draw_multiline
// Draws multi-line HUD text with per-character fade.
// Counts newlines in text, computes overall alpha from timing fields,
// selects best font, centers horizontally, draws with shadowed renderer.
// Color includes fade alpha packed into ARGB.
// ============================================================================
void hud_text_draw_multiline(void) {
    int* element = gx_begin_overlay();  // FUN_803826f0

    // Count lines
    u32 line_count = 1;
    for (int i = 0; ((char*)(element + 1))[i] != '\0'; i++) {
        if (((char*)(element + 1))[i] == '\n') {
            line_count++;
        }
    }

    set_render_layer(1);

    // Compute fade alpha from timing
    float fade_start = *(float*)(element + 0x148/4);
    float current    = *(float*)(element + 0x144/4);
    double alpha = 1.0;

    if (fade_start < current) {
        float fade_end = *(float*)(element + 0x150/4);
        alpha = (double)(1.0f - (current - fade_start) / (fade_end - fade_start));
    }

    u32 alpha_byte = gx_set_alpha((float)(0.5f * alpha));  // FUN_803825c0

    hud_select_font_for_width();

    // Measure text width and center
    double text_width = text_get_char_width((u8*)(element + 1));

    int vp_left = g_viewport_offset_left + g_viewport_data[0];
    int vp_right = g_viewport_data[1] - g_viewport_offset_right;
    float center_x = (float)(vp_left + ((vp_right - vp_left) >> 1));

    // Get Y from row count
    u32 y_pos = hud_get_row_y(line_count);

    // Compute draw X (centered)
    float draw_x = -(float)(text_width * 0.02 - (double)center_x);

    // Pack alpha into color
    u32 main_color  = alpha_byte | 0x40646400;
    u32 shadow_color = alpha_byte | 0x5000;

    text_draw_char_shadowed((double)draw_x, (double)(float)y_pos,
                            element + 1, main_color, shadow_color);

    gx_end_overlay();
}
