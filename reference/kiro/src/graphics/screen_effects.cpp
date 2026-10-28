// ============================================================================
// Screen Effects System
// Handles lens flares, screen overlays, damage flash, and per-player HUD effects.
// Batch 323 | Date: 2026-08-25
// ============================================================================

#include "common.h"
#include "graphics.h"
#include "sprite.h"

// ---- Globals ----
static int g_lens_flare_count;                    // DAT_80611f70 - active flare entries
static LensFlareEntry g_lens_flare_queue[8];      // DAT_805785c0 - 0x18 bytes each

static int g_screen_effect_slot_count;            // DAT_80611fec - allocated slot count (max 17)
static u8 g_screen_effect_pool[17 * 0x144];      // pool at 0x8057e9c8

static float g_screen_effect_scale_x;            // FLOAT_80610828
static float g_screen_effect_scale_y;            // FLOAT_8061082c
static float g_screen_effect_scale_z;            // FLOAT_80610830

static int g_screen_effect_active;               // DAT_80611fe8
static int g_screen_effect_flags;                // DAT_80611f78
static int g_screen_effect_frame;                // DAT_80611f7c
static float g_screen_effect_timer;              // FLOAT_80611f80
static int g_screen_effect_state;                // DAT_80611f84
static float g_screen_effect_fade_a;             // FLOAT_80611ff8
static float g_screen_effect_fade_b;             // FLOAT_80611ff4
static float g_screen_effect_fade_c;             // FLOAT_80611ff0
static int g_render_target_id;                   // DAT_80610834

// ---- Constants (read from .rodata) ----
static const float SCALE_NORMAL    = 1.0f;       // FLOAT_80615718
static const float SCALE_SPLIT     = 0.5f;       // FLOAT_8061571c (approximate)
static const float SCALE_WIDE_ADJ  = 0.75f;      // FLOAT_8061572c (approximate)
static const float FADE_DEFAULT    = 0.0f;       // FLOAT_80615730
static const float DAMAGE_SCALE    = 0.01f;      // FLOAT_80615748 (approximate)

// ============================================================================
// Address: 0x80103ea8 | lens_flare_render_all
// Iterates lens flare queue, renders flares matching current camera ID.
// ============================================================================
void lens_flare_render_all(void) {
    gx_begin_overlay();       // FUN_803826f0
    set_render_layer(2);      // FUN_8016f00c

    for (int i = 0; i < g_lens_flare_count; i++) {
        if (g_lens_flare_queue[i].camera_id == get_active_camera_id()) {
            lens_flare_render_screen();
        }
    }

    gx_end_overlay();         // FUN_8038273c
}

// ============================================================================
// Address: 0x80103f20 | lens_flare_queue_add
// Adds a lens flare entry (max 8). Called from shadow projection.
// Params: camera_id, intensity, position (vec3*), type
// ============================================================================
void lens_flare_queue_add(int camera_id, int intensity, float* position, int type) {
    if (g_lens_flare_count >= 8) {
        return;
    }

    int idx = g_lens_flare_count;
    g_lens_flare_count++;

    g_lens_flare_queue[idx].camera_id  = camera_id;
    g_lens_flare_queue[idx].intensity  = intensity;
    g_lens_flare_queue[idx].pos_x      = position[0];
    g_lens_flare_queue[idx].pos_y      = position[1];
    g_lens_flare_queue[idx].pos_z      = position[2];
    g_lens_flare_queue[idx].type       = type;
}

// ============================================================================
// Address: 0x80103f6c | screen_effect_preload_textures
// Preloads 6 fixed sprite texture sets plus per-player textures.
// Called from game_subsystems_update during level load.
// ============================================================================
void screen_effect_preload_textures(SpriteLoadContext* ctx) {
    // Load 6 fixed screen-effect texture sets
    ctx = sprite_load_texture_set(ctx);
    ctx = sprite_load_texture_set(ctx);
    ctx = sprite_load_texture_set(ctx);
    ctx = sprite_load_texture_set(ctx);
    ctx = sprite_load_texture_set(ctx);
    ctx = sprite_load_texture_set(ctx);

    // Load per-player textures
    int player_count = get_max_local_players();  // FUN_802c7838
    for (int i = 0; i < player_count; i++) {
        int slot = get_player_texture_slot(i);   // FUN_802c7770
        if (slot != -1) {
            ctx = sprite_load_texture_set(ctx);
        }
    }
}

// ============================================================================
// Address: 0x80103fec | screen_effect_load_mode_textures
// Conditionally loads textures based on game mode:
//   0x13 = arcade, 0x09 = story, 0x06 = mapmaker, 0x02 = multiplayer
// ============================================================================
void screen_effect_load_mode_textures(SpriteLoadContext* ctx) {
    u8 game_mode = g_game_mode;         // DAT_805015be
    u8 sub_mode  = g_game_sub_mode;     // DAT_805015bd

    if (game_mode == 0x13) {
        // Arcade mode: 1 texture set
        ctx = sprite_load_texture_set(ctx);
    } else if (game_mode == 0x09) {
        // Story mode: 1 texture set
        ctx = sprite_load_texture_set(ctx);
    } else if (sub_mode == 0x06) {
        // Mapmaker: 2 texture sets
        ctx = sprite_load_texture_set(ctx);
        ctx = sprite_load_texture_set(ctx);
    } else if (sub_mode == 0x02) {
        // Multiplayer: 2 texture sets
        ctx = sprite_load_texture_set(ctx);
        ctx = sprite_load_texture_set(ctx);
    }

    // HUD textures if HUD is enabled
    if (is_hud_enabled()) {                       // FUN_8011f768
        if ((g_game_flags & 0x100000) == 0) {     // DAT_805015c0
            int total_players = g_num_players_a + g_num_players_b;  // DAT_80501572 + DAT_80501570
            for (int i = 0; i < total_players; i++) {
                int slot = get_player_hud_slot(i);  // FUN_802c7788
                if (slot != -1) {
                    ctx = sprite_load_texture_set(ctx);
                }
            }
        }
        // Final extra texture
        sprite_load_texture_set(ctx);
    }
}

// ============================================================================
// Address: 0x801040f4 | screen_effect_system_init
// Full initialization: sets render scale based on screen mode, clears all
// effect buffers, initializes subsystems, sets default fade values.
// ============================================================================
void screen_effect_system_init(void) {
    int screen_mode = get_screen_mode();  // FUN_801c1714

    // Set base scale factor by screen layout
    if (screen_mode == 2) {
        g_screen_effect_scale_x = SCALE_SPLIT;
    } else if (screen_mode == 1) {
        g_screen_effect_scale_x = SCALE_NORMAL;
    } else {
        g_screen_effect_scale_x = 0.333f;  // FLOAT_80615720 (3-way split)
    }

    // Compute scaled Y offset from frame counters
    g_screen_effect_scale_z = g_base_scale *
        ((float)(g_frame_counter_a - BIAS) / (float)(g_frame_counter_b - BIAS)) / SCALE_FACTOR;

    // Compute scaled camera offset with sign flip
    g_screen_effect_scale_y = (float)(g_camera_offset ^ 0x80000000 - BIAS_B) * OFFSET_SCALE;

    // Apply widescreen adjustment
    float ws_factor = is_widescreen() ? SCALE_WIDE_ADJ : SCALE_NORMAL;
    g_screen_effect_scale_y *= ws_factor;

    float ws_factor2 = is_widescreen() ? SCALE_WIDE_ADJ : SCALE_NORMAL;
    g_screen_effect_scale_z *= ws_factor2;

    // Clear all effect buffers
    g_screen_effect_slot_count = 0;
    memset(g_effect_buffer_main, 0, 0x5df0);
    memset(g_effect_buffer_small, 0, 0x20);
    memset(g_effect_buffer_mid, 0, 0x500);
    g_screen_effect_active = 0;
    memset(g_effect_buffer_tiny, 0, 0x10);

    // Set default fade values
    g_screen_effect_fade_a = FADE_DEFAULT;
    g_screen_effect_fade_b = FADE_DEFAULT + 0.01f;  // FLOAT_80615734
    g_screen_effect_fade_c = FADE_DEFAULT;

    // Reset state tracking
    g_screen_effect_frame = 0;
    g_screen_effect_timer = FADE_DEFAULT;
    g_screen_effect_state = 0;

    // Initialize subsystems
    screen_effect_sub_init_a();      // FUN_80105750
    screen_effect_sub_init_b();      // FUN_8012b524
    screen_effect_sub_init_c();      // FUN_80111c88
    screen_effect_sub_init_d();      // FUN_8035c694

    g_screen_effect_flags = 0;
    screen_effect_sub_init_e();      // FUN_8011d710
    screen_effect_sub_init_f();      // FUN_8011e6b4
    screen_effect_sub_init_g();      // FUN_80122e40
    screen_effect_sub_init_h();      // FUN_8012b4d0

    // Initialize 10 interpolation slots to default value
    for (int i = 0; i < 10; i++) {
        g_effect_interp_table[i] = FADE_DEFAULT;  // DAT_8057e9a0
    }

    screen_effect_sub_init_i();      // FUN_80357cc4
    screen_effect_sub_init_j();      // FUN_8011db84

    g_render_target_id = -1;         // DAT_80610834
}

// ============================================================================
// Address: 0x801042e0 | screen_effect_system_reset
// Level transition reset: flushes subsystem, then reinitializes everything.
// ============================================================================
void screen_effect_system_reset(void) {
    screen_effect_flush_pending();    // FUN_80357cc0
    screen_effect_sub_reset();        // FUN_8011dafc
    screen_effect_sub_init_h();       // FUN_8012b4d0
    screen_effect_system_init();
}

// ============================================================================
// Address: 0x8010430c | screen_effect_get_damage_alpha
// Returns damage flash alpha: 1.0 = no damage, 0.0 = full screen red.
// Clamped to [0.0, 1.0] based on damage timer * scale.
// ============================================================================
float screen_effect_get_damage_alpha(void) {
    int player_offset = g_active_player_index * 0x177c;  // DAT_80611d74 + 4

    if (g_player_damage_active[player_offset] == 0) {
        return 1.0f;
    }

    float alpha = DAMAGE_SCALE * g_player_damage_timer[player_offset];
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    return 1.0f - alpha;
}

// ============================================================================
// Address: 0x80104364 | screen_effect_alloc_slot
// Allocates a 0x144-byte screen effect slot from a 17-entry pool.
// Returns pointer to slot, or NULL if pool is full.
// ============================================================================
void* screen_effect_alloc_slot(void) {
    if (g_screen_effect_slot_count >= 17) {
        return NULL;
    }

    int idx = g_screen_effect_slot_count;
    g_screen_effect_slot_count++;

    return &g_screen_effect_pool[idx * 0x144];
}

// ============================================================================
// Address: 0x80104394 | screen_effect_init_slot
// Initializes a screen effect slot with per-band threshold flags from
// distance comparison tables. Supports secondary entity (type == 8).
// Params: slot (0x144 bytes), entity data pointer
// ============================================================================
void screen_effect_init_slot(int* slot, int* entity) {
    slot[0x50] = 0;  // offset 0x140 = cleared (float cast to 0.0)

    for (int band = 0; band < 20; band++) {
        // Primary threshold: distance <= threshold * scale
        slot[band] = (int)(entity[0x77] * g_threshold_table_a[band] <= entity[0x73]);

        // Interpolation slot (offset 0xA0)
        slot[band + 0x28] = 0;  // float 0.0

        // Secondary flag (offset 0x50)
        slot[band + 0x14] = 0;

        // Extra slot (offset 0xF0)
        slot[band + 0x3C] = 0;  // float 0.0

        // If entity type == 8 (has secondary source)
        if (entity[8] == 8) {
            int* secondary = (int*)entity[0x1F];  // offset 0x7C
            slot[band + 0x14] = (int)(secondary[0x1F] * g_threshold_table_b[band]
                                      <= secondary[0x1E]);
        }
    }
}

// ============================================================================
// Address: 0x80104438 | screen_effect_get_aspect_scale
// Returns widescreen-adjusted scale for split-screen modes.
// Mode 4 = split (always wide), Mode 3 = conditional on player count.
// ============================================================================
float screen_effect_get_aspect_scale(void) {
    int screen_mode = get_screen_mode();  // FUN_801c1714

    if (screen_mode == 4) {
        goto use_wide_scale;
    }

    if (screen_mode == 3 && g_player_context != NULL) {
        if (g_active_player_count > 0) {
            goto use_wide_scale;
        }
    }

    return SCALE_NORMAL;

use_wide_scale:
    if (is_widescreen()) {
        return 0.375f;  // FLOAT_8061574c (widescreen split)
    } else {
        return SCALE_SPLIT;
    }
}
