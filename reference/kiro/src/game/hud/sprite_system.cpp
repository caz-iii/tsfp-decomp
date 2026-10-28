// Address: 0x8004d7f4 | Name: sprite_set_virtual_screen | Date: 2026-08-10
// Address: 0x8004d81c | Name: sprite_set_virtual_screen_from_rect | Date: 2026-08-10
// Address: 0x8004d850 | Name: sprite_get_texture_id | Date: 2026-08-10
// Address: 0x8004d868 | Name: sprite_get_texture_name | Date: 2026-08-10
// Address: 0x8004d880 | Name: sprite_check_visibility_flags | Date: 2026-08-10
// Address: 0x8004dab0 | Name: sprite_load_texture_set | Date: 2026-08-10
// Address: 0x8004dc40 | Name: sprite_load_single_texture | Date: 2026-08-10
//
// Sprite/Texture Resource System
// Manages sprite sheets, virtual screen coordinate mapping, texture loading
// via the streaming queue, and per-mode visibility filtering.
//
// Texture paths:
//   Single-frame: "assets/ts/textures/gamecube/%s.raw"
//   Multi-frame:  "assets/ts/textures/gamecube/%s%d.raw"
//   Direct name:  "assets/ts/textures/gamecube/%s"
//
// Sprite table layout (stride 0x18 = 24 bytes per entry):
//   +0x00: frame_count (u8)
//   +0x04: flags
//   +0x08: type/format
//   +0x0C: texture_name (char*)
//   +0x10: slot_handle
//   +0x14: texture_id (u32, -1 = unloaded)

#include "common.h"
#include "sprite.h"

// Virtual screen globals (used for normalized-to-pixel coordinate conversion)
static f32 g_virtual_screen_top;    // FLOAT_80611a70
static f32 g_virtual_screen_left;   // FLOAT_80611a6c
static f32 g_virtual_screen_width;  // FLOAT_80611a68
static f32 g_virtual_screen_height; // FLOAT_80611a64
static f32 g_virtual_scale_x;       // FLOAT_80611a60
static f32 g_virtual_scale_y;       // FLOAT_80611a5c

static const f32 SCALE_UNIT = 1.0f;        // FLOAT_80613d60
static const f32 SCREEN_PERCENT = 100.0f;  // FLOAT_80613d64

// Sprite table: array of sprite set descriptors
// Located at 0x803f7d58, stride 0x18 (24 bytes)
extern u8 g_sprite_table[];       // DAT_803f7d58
extern char** g_sprite_names[];   // PTR_s_filled16x16_803f7d64 (offset +0x0C)
extern u32 g_sprite_tex_ids[];    // DAT_803f7d6c (offset +0x14)

// Texture slot tracking
extern int g_sprite_slot_cursor;   // DAT_80611a74
extern u32 g_sprite_load_slots[];  // DAT_80526438 (3 entries, stride 0x10)

// Game mode for visibility filtering
extern int g_current_screen_mode;  // DAT_80612614
extern u32 g_game_flags;           // DAT_805015c0
extern u8 g_difficulty_level;      // DAT_805015bd
extern u8 g_player_count;          // DAT_805015be

// External functions
extern int texture_pool_begin(void);                    // FUN_803826e0
extern void texture_pool_end(void);                     // FUN_8038272c
extern int texture_load_async(/* ... */);               // FUN_80387fb8
extern void stream_queue_animation(void);
extern int game_is_online(void);                        // FUN_80370018
extern u16* award_get_entry(int award_id);

// -----------------------------------------------------------------------------
// Address: 0x8004d7f4
// Sets virtual screen dimensions and computes inverse scale factors.
// Called by sprite_set_virtual_screen_from_rect and the main render init.
// scale_x = 1.0 / width, scale_y = 1.0 / height
// -----------------------------------------------------------------------------
void sprite_set_virtual_screen(f32 left, f32 top, f32 width, f32 height)
{
    g_virtual_screen_top    = left;
    g_virtual_screen_left   = top;
    g_virtual_screen_width  = width;
    g_virtual_screen_height = height;
    g_virtual_scale_x       = SCALE_UNIT / width;
    g_virtual_scale_y       = SCALE_UNIT / height;
}

// -----------------------------------------------------------------------------
// Address: 0x8004d81c
// Converts a bounding rect (left, top, right, bottom) to virtual screen dims.
// width = (right - left) / 100.0, height = (bottom - top) / 100.0
// Used by outer_game_loop, hud_element_push, hud_element_update, etc.
// -----------------------------------------------------------------------------
void sprite_set_virtual_screen_from_rect(f32 left, f32 top, f32 right, f32 bottom)
{
    f32 width  = (right - left) / SCREEN_PERCENT;
    f32 height = (bottom - top) / SCREEN_PERCENT;
    sprite_set_virtual_screen(left, top, width, height);
}

// -----------------------------------------------------------------------------
// Address: 0x8004d850
// Returns the texture resource ID for sprite set entry at given index.
// Table lookup: g_sprite_table[index * 6] at DWORD offset +0x14 (= g_sprite_tex_ids)
// Returns -1 (0xFFFFFFFF) if texture not yet loaded.
// -----------------------------------------------------------------------------
u32 sprite_get_texture_id(int index)
{
    return g_sprite_tex_ids[index * 6];
}

// -----------------------------------------------------------------------------
// Address: 0x8004d868
// Returns the texture filename string for a sprite set entry.
// Table lookup: g_sprite_table[index * 6] at pointer offset +0x0C (= g_sprite_names)
// Used by sprite_load_texture_set to build the streaming path.
// -----------------------------------------------------------------------------
char* sprite_get_texture_name(int index)
{
    return (char*)g_sprite_names[index * 6];
}

// -----------------------------------------------------------------------------
// Address: 0x8004d880
// Determines if a sprite/element should be visible based on bitfield flags
// and the current game mode (g_current_screen_mode).
//
// Mode values (0x65-0x6B map to specific game screen contexts):
//   0x65: Story mode screen - checks flag bits 0x201000, filtered by game flags
//   0x66: Arcade select     - checks bit 11
//   0x67: Challenge mode    - flags 0x8000, 0x40000000, 0x10000000
//   0x68: Multiplayer       - checks bit 16
//   0x69: MapMaker          - checks bit 26
//   0x6A: Options           - checks bit 31
//   0x6B: Extras            - checks bit 29
//   Default (1-0x53, 100): Award/level modes with complex flag logic
//
// Returns: 1 = visible, 0 = hidden
// Flag bit 0x1000000 forces hidden (early exit).
// Flag 0x40000000 = "always visible unless game flag 0x100000 set"
// -----------------------------------------------------------------------------
u32 sprite_check_visibility_flags(u32 flags)
{
    u32 result;

    // Bit 24 set = unconditionally hidden
    if ((flags & 0x01000000) != 0) {
        return 0;
    }

    // Special "always-on" flag
    if (flags == 0x40000000) {
        result = 0;
        if (((g_game_flags & 0x100000) == 0) || (g_current_screen_mode == 0x67)) {
            result = 1;
        }
        return result;
    }

    switch (g_current_screen_mode) {
    case 0x65: // Story mode
        if ((g_game_flags & 0x200000) == 0) {
            result = (u32)((flags & 0x201000) != 0);
        } else {
            result = 0;
        }
        break;

    case 0x66: // Arcade select
        result = (flags >> 11) & 1;
        break;

    case 0x67: // Challenge mode
        result = 0;
        if (((flags & 0x8000) != 0) || (flags == 0x40000000) || (flags == 0x10000000)) {
            result = 1;
        }
        break;

    case 0x68: // Multiplayer
        result = (flags >> 16) & 1;
        break;

    case 0x69: // MapMaker
        result = (flags >> 26) & 1;
        break;

    case 0x6A: // Options
        result = (flags >> 31);
        break;

    case 0x6B: // Extras
        result = (flags >> 29) & 1;
        break;

    default:
        // Award/level modes (1-0x53) and mode 0x64
        if ((g_current_screen_mode >= 1 && g_current_screen_mode <= 0x53) ||
            (g_current_screen_mode == 100)) {
            // Difficulty-gated content (difficulty == 10 means "all difficulties")
            if ((g_difficulty_level == 10) && (g_current_screen_mode != 100)) {
                u16* award_entry = award_get_entry(g_current_screen_mode);
                if (award_entry != NULL && ((*(u32*)(award_entry + 4) & flags) != 0)) {
                    result = 1;
                    break;
                }
            }
            // Various flag checks in priority order
            if (((flags & 0x800000) != 0) && (g_current_screen_mode == 100)) {
                result = 1;
            } else if (((flags & 0x20000) != 0) && (g_difficulty_level != 10)) {
                result = 1;
            } else if (((flags & 0x400000) != 0) && ((g_game_flags & 0x40) != 0)) {
                result = 1;
            } else if ((flags & 0x200000) != 0) {
                result = 1;
            } else if ((g_player_count == 12) && ((flags & 0x10000000) != 0)) {
                // Player count 12 ('\f') with flag bit 28
                result = 1;
            } else {
                // Default: bit 13
                result = (flags >> 13) & 1;
            }
        } else {
            // Non-standard mode: check online flag (bit 27)
            if (((flags & 0x8000000) == 0) || (game_is_online() == 0)) {
                result = 0;
            } else {
                result = 1;
            }
        }
        break;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Address: 0x8004dab0
// Loads a complete sprite texture set (potentially multi-frame).
// 1. Validates sprite index is in range [0, 0x3F8)
// 2. If slot not already loaded (handle == 0), allocates and streams textures
// 3. Single-frame sprites: "assets/ts/textures/gamecube/%s.raw"
//    Multi-frame sprites:  "assets/ts/textures/gamecube/%s%d.raw" (1-indexed)
// 4. Queues each frame for streaming via stream_queue_animation
//
// Called by many HUD/menu systems to preload UI sprite sheets.
// g_sprite_slot_cursor tracks the texture pool allocation position.
// -----------------------------------------------------------------------------
void sprite_load_texture_set(void)
{
    int pool_index;
    int slot_cursor;
    int frame;
    int load_result;

    pool_index = texture_pool_begin();
    slot_cursor = g_sprite_slot_cursor;

    if (pool_index < 0 || pool_index >= 0x3F8) {
        texture_pool_end();
        return;
    }

    // Initialize 3 texture load slot descriptors
    g_sprite_load_slots[0] = 0xFFFFFFFF; // tex_id = unloaded
    g_sprite_load_slots[3] = 0;          // flags
    g_sprite_load_slots[2] = 3;          // format
    g_sprite_load_slots[4] = 0xFFFFFFFF;
    g_sprite_load_slots[7] = 0x20000;    // size
    g_sprite_load_slots[6] = 3;
    g_sprite_load_slots[8] = 0xFFFFFFFF;
    g_sprite_load_slots[11] = 0x20000;

    // Check if this sprite set is already loaded
    if (*(int*)(&g_sprite_table[pool_index * 0x18 + 0x10]) == 0) {
        // Mark as loaded with current slot position
        g_sprite_tex_ids[pool_index * 6] = 0xFFFFFFFF;
        *(int*)(&g_sprite_table[pool_index * 0x18 + 0x10]) = slot_cursor;

        // Load each frame
        u8 frame_count = g_sprite_table[pool_index * 0x18];
        for (frame = 0; frame < (int)(u32)frame_count; frame++) {
            char* tex_name = sprite_get_texture_name(pool_index);

            if (frame_count < 2) {
                // Single-frame: "assets/ts/textures/gamecube/%s.raw"
                load_result = texture_load_async(
                    g_sprite_slot_cursor,
                    "assets/ts/textures/gamecube/%s.raw",
                    tex_name,
                    0x20000, 3, g_sprite_load_slots, 0, 0xFFFFFFFF
                );
            } else {
                // Multi-frame: "assets/ts/textures/gamecube/%s%d.raw"
                int frame_num = frame + 1;
                load_result = texture_load_async(
                    g_sprite_slot_cursor,
                    "assets/ts/textures/gamecube/%s%d.raw",
                    tex_name,
                    frame_num, 3, g_sprite_load_slots, 0, 0xFFFFFFFF
                );
            }

            // Queue animation frame for streaming
            stream_queue_animation();

            // Advance slot cursor past loaded data
            g_sprite_slot_cursor = load_result + g_sprite_slot_cursor + 1;
        }
    }

    texture_pool_end();
}

// -----------------------------------------------------------------------------
// Address: 0x8004dc40
// Loads a single named texture by path.
// Uses format string "assets/ts/textures/gamecube/%s" with the provided name.
// Fixed flag 0x80400000 indicates high-priority single load.
// Thin wrapper around texture_load_async.
// -----------------------------------------------------------------------------
void sprite_load_single_texture(int slot, char* name, u32 param_11, u32 param_12,
                                u32 param_13, u32 param_14, u32 param_15, u32 param_16)
{
    texture_load_async(
        slot,
        "assets/ts/textures/gamecube/%s",
        name,
        0x80400000,
        param_13, param_14, param_15, param_16
    );
}

// Address: 0x8004dc70 | Name: sprite_load_all_visible | Date: 2026-08-10
// Address: 0x8004dd58 | Name: sprite_load_by_flag_mask | Date: 2026-08-10
// Address: 0x8004ddc0 | Name: sprite_resolve_all_textures | Date: 2026-08-10
// Address: 0x8004df6c | Name: sprite_replace_texture_id | Date: 2026-08-10
// Address: 0x8004dfa0 | Name: sprite_system_init | Date: 2026-08-10
// Address: 0x8004e014 | Name: sprite_finalize_load | Date: 2026-08-10
// Address: 0x8004e034 | Name: sprite_update_pulse_alpha | Date: 2026-08-10
// Address: 0x8004e074 | Name: sprite_draw_progress_bar | Date: 2026-08-10
// Address: 0x8004e638 | Name: sprite_draw_icon | Date: 2026-08-10
// Address: 0x8004e6dc | Name: sprite_draw_ui_element | Date: 2026-08-10

// ============================================================================
// Sprite System - Loading, Resolution, and Drawing
// ============================================================================

#define SPRITE_TABLE_COUNT  0x3F8  // 1016 sprite set entries
#define CURSOR_COUNT        0x4B   // 75 mapmaker cursor textures
#define CURSOR_STRIDE       0x26   // 38 bytes per cursor slot

// Mapmaker cursor visibility table (75 entries, 4 bytes each)
extern u32 g_cursor_vis_flags[];      // DAT_803fe740
// Mapmaker cursor texture IDs (75 entries)
extern u32 g_cursor_tex_ids[];        // DAT_80526468
// Mapmaker cursor slot data
extern u8 g_cursor_slots[];           // DAT_80526594

// Cached common texture IDs for quick draw calls
extern u32 g_cached_tex_ids[];        // DAT_804b1100 (array of ~12 IDs)

// Sprite palette lookup tables (for mode <100 vs >=100)
extern u32 g_sprite_palette_normal[]; // DAT_803fe718
extern u32 g_sprite_palette_extra[];  // DAT_803fe728
extern u32* g_active_palette;         // DAT_80611a58

// Sprite system state
extern int g_sprite_textures_ready;   // DAT_80611a54
extern int g_sprite_pool_base;        // DAT_80611a78

// Pulse alpha state
extern u8 g_pulse_alpha_max;          // DAT_80611a50
extern u8 g_pulse_alpha_cur;          // DAT_80611a51
extern u32 g_frame_counter;           // DAT_80612028

// External rendering functions
extern void render_begin_sprites(void);             // FUN_803826e4 / e8 / ec
extern void render_end_sprites(void);               // FUN_80382730 / 34 / 38
extern void render_set_texture(int tex_id);         // FUN_8016e77c
extern void render_begin_blend(void);               // FUN_8017c658
extern void render_end_blend(void);                 // FUN_8017c670
extern void render_draw_quad_uv(int x0, int y0, int x1, int y1,
                                int u0, int v0, int u1, int v1);  // FUN_80170410
extern void render_draw_quad_uv_blended(f64 x0, f64 y0, f64 x1, f64 y1,
                                        int u0, int v0, int u1, int v1);  // FUN_80170b10
extern int stream_find_anim_by_name(void);

// Texture descriptor struct (stride 0x58)
// +0x4E: width (u16)
// +0x52: height (u16)
extern int* g_texture_descriptor_base;  // DAT_80610a20

// -----------------------------------------------------------------------------
// Address: 0x8004dc70
// Loads ALL visible sprite texture sets + mapmaker cursors.
// Iterates all 1016 sprite entries, checks visibility for current mode,
// loads matching sets via sprite_load_texture_set.
// Then loads 75 mapmaker cursor textures from "textures/misc/mm_cursor%d.raw".
// -----------------------------------------------------------------------------
void sprite_load_all_visible(void)
{
    int i;

    render_begin_sprites();

    // Load all sprite sets that pass visibility check
    for (i = 0; i < SPRITE_TABLE_COUNT; i++) {
        u32 flags = *(u32*)(&g_sprite_table[i * 0x18 + 0x08]);  // visibility flags at +0x08
        if (sprite_check_visibility_flags(flags) != 0) {
            sprite_load_texture_set();
        }
    }

    // Load mapmaker cursor textures (75 entries)
    for (i = 0; i < CURSOR_COUNT; i++) {
        g_cursor_tex_ids[i] = 0xFFFFFFFF;  // mark unloaded
        if (sprite_check_visibility_flags(g_cursor_vis_flags[i]) != 0) {
            texture_load_async(
                (int)&g_cursor_slots[i * CURSOR_STRIDE],
                "textures/misc/mm_cursor%d.raw",
                i + 1,  // 1-indexed
                /* format params... */
            );
            stream_queue_animation();
            g_cursor_tex_ids[i] = 0;  // mark pending
        }
    }

    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004dd58
// Loads sprite sets matching a given bitfield mask (selective preload).
// Used when only a specific subsystem's sprites are needed.
// Checks (mask & entry_flags) != 0 rather than full visibility evaluation.
// -----------------------------------------------------------------------------
void sprite_load_by_flag_mask(void)
{
    u32 mask;
    int i;

    mask = render_begin_sprites();  // returns current flag mask context

    for (i = 0; i < SPRITE_TABLE_COUNT; i++) {
        u32 flags = *(u32*)(&g_sprite_table[i * 0x18 + 0x08]);
        if ((mask & flags) != 0) {
            sprite_load_texture_set();
        }
    }

    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004ddc0
// Resolves streaming handles to final texture IDs for all loaded sprites.
// Called after streaming is complete to make textures drawable.
//
// 1. Iterates all 1016 sprites: if loaded (handle != 0), resolves to tex ID
// 2. Caches 9+ common texture IDs into g_cached_tex_ids for fast icon drawing:
//    [0]=0x29C, [1]=0x2C2, [2]=0x29D, [3]=0x2C4, [4]=0x29E,
//    [8]=0x2A0, [9]=0x2A3, [10]=0x2A4, [11]=0x2A5
// 3. Resolves mapmaker cursor texture IDs (75 entries)
// 4. Sets active palette table based on game mode (normal vs extra)
// -----------------------------------------------------------------------------
void sprite_resolve_all_textures(void)
{
    int i;
    int handle;

    render_begin_sprites();
    g_sprite_textures_ready = 1;

    // Resolve sprite texture sets
    for (i = 0; i < SPRITE_TABLE_COUNT; i++) {
        // Skip "always-on" sprites - keep existing tex ID
        if (*(u32*)(&g_sprite_table[i * 0x18 + 0x08]) != 0x40000000) {
            g_sprite_tex_ids[i * 6] = 0xFFFFFFFF;  // reset
        }
        // If streaming handle is valid, resolve
        int slot_handle = *(int*)(&g_sprite_table[i * 0x18 + 0x10]);
        if (slot_handle != 0) {
            int resolved = stream_find_anim_by_name();
            if (resolved != 0) {
                g_sprite_tex_ids[i * 6] = *(u32*)(resolved + 0x14);
            }
        }
    }

    // Cache common UI texture IDs
    g_cached_tex_ids[0] = sprite_get_texture_id(0x29C);  // crosshair
    g_cached_tex_ids[1] = sprite_get_texture_id(0x2C2);
    g_cached_tex_ids[2] = sprite_get_texture_id(0x29D);
    g_cached_tex_ids[3] = sprite_get_texture_id(0x2C4);
    g_cached_tex_ids[4] = sprite_get_texture_id(0x29E);
    g_cached_tex_ids[8] = sprite_get_texture_id(0x2A0);
    g_cached_tex_ids[9] = sprite_get_texture_id(0x2A3);
    g_cached_tex_ids[10] = sprite_get_texture_id(0x2A4);
    g_cached_tex_ids[11] = sprite_get_texture_id(0x2A5);

    // Resolve mapmaker cursor textures
    for (i = 0; i < CURSOR_COUNT; i++) {
        if ((int)g_cursor_tex_ids[i] >= 0) {  // pending (was set to 0)
            int resolved = stream_find_anim_by_name();
            g_cursor_tex_ids[i] = *(u32*)(resolved + 0x14);
        }
    }

    // Select palette table based on game mode
    g_active_palette = (u32*)&g_sprite_palette_normal;
    if (g_current_screen_mode > 100) {
        g_active_palette = (u32*)&g_sprite_palette_extra;
    }

    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004df6c
// Replaces all occurrences of old_tex_id with new_tex_id in the sprite table.
// Used for texture hot-swapping (team colors, state changes, skin variants).
// -----------------------------------------------------------------------------
void sprite_replace_texture_id(int old_tex_id, int new_tex_id)
{
    int i;

    for (i = 0; i < SPRITE_TABLE_COUNT; i++) {
        if (g_sprite_tex_ids[i * 6] == (u32)old_tex_id) {
            g_sprite_tex_ids[i * 6] = (u32)new_tex_id;
        }
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8004dfa0
// Initializes the sprite system for a new screen/mode transition.
// Allocates 0x5000 bytes (20KB) for the texture slot pool.
// Resets all sprite slot handles, preserving "always-on" (0x40000000) sprites
// unless in challenge mode (0x67).
// -----------------------------------------------------------------------------
void sprite_system_init(void)
{
    int i;
    int mode;

    // Allocate texture pool memory
    g_sprite_slot_cursor = (int)mem_alloc(0x5000, 1);
    mode = g_current_screen_mode;
    g_sprite_pool_base = g_sprite_slot_cursor;

    // Reset sprite slot handles (clear loaded state)
    for (i = 0; i < SPRITE_TABLE_COUNT; i++) {
        u32 flags = *(u32*)(&g_sprite_table[i * 0x18 + 0x08]);
        // Preserve "always-on" sprites unless challenge mode
        if (flags != 0x40000000 || mode == 0x67) {
            *(int*)(&g_sprite_table[i * 0x18 + 0x10]) = 0;
        }
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8004e014
// Wrapper: triggers texture resolution after streaming load is complete.
// Simply calls sprite_resolve_all_textures.
// -----------------------------------------------------------------------------
void sprite_finalize_load(void)
{
    sprite_resolve_all_textures();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e034
// Updates the sprite pulse alpha using a triangle wave from the frame counter.
// Produces a 128-frame oscillation cycle: 0→64→0 (mapped to 64→128→64).
// Bit 5 of frame counter doubles max to 0x80 for emphasis beats.
// Used for blinking/pulsing UI focus highlights.
// -----------------------------------------------------------------------------
void sprite_update_pulse_alpha(void)
{
    u8 wave = (u8)(g_frame_counter & 0x7F);

    // Triangle wave: fold at midpoint
    if (wave > 0x3F) {
        wave = 0x80 - wave;  // reflect second half
    }

    g_pulse_alpha_max = 0x40;
    g_pulse_alpha_cur = wave + 0x40;  // offset to 64-128 range

    // Double amplitude on emphasis frames (bit 5 set)
    if ((g_frame_counter & 0x20) != 0) {
        g_pulse_alpha_max = 0x80;
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8004e074
// Draws a horizontal progress bar using sprite 0x103 (3-segment 9-slice).
// The bar texture has: left cap (8px), middle stretch, right cap (8px).
// Supports bidirectional fill: left-to-right (direction=0) or right-to-left.
//
// param_3: total bar width in pixels
// param_4: bar height
// direction (in_r10): 0=fill from left, 1=fill from right
// Fill amount controlled by a normalized float (clamped 0.0-1.0).
//
// Renders in two passes: top half (blended) + bottom half (standard).
// Used for health bars, ammo bars, loading bars, timer displays.
// -----------------------------------------------------------------------------
void sprite_draw_progress_bar(u32 bar_width, int bar_height, int direction)
{
    int base_x, base_y;
    int fill_pixels, empty_start;
    int tex_id;

    // Get render position from sprite stack
    u64 pos = render_begin_sprites();
    base_x = (int)(pos >> 32);
    base_y = (int)pos;
    int bottom = base_y + bar_height;

    // Bind progress bar texture (sprite index 0x103)
    tex_id = sprite_get_texture_id(0x103);
    render_set_texture(tex_id);
    render_begin_blend();

    // Clamp fill value to [0.0, 1.0]
    f32 fill = /* from float register */;
    if (fill > 1.0f) fill = 1.0f;
    if (fill < 0.0f) fill = 0.0f;

    // Calculate pixel fill amount
    if (direction == 0) {
        // Left-to-right fill
        fill_pixels = (int)(fill * (f32)bar_width);
        empty_start = 0;
    } else {
        // Right-to-left fill
        empty_start = (int)((1.0f - fill) * (f32)bar_width);
        fill_pixels = bar_width - empty_start;
    }

    // Draw filled portion (top half - blended pass)
    // Split into 3 segments: left cap (8px), middle, right cap (8px)
    
    // Left cap (UV: 0-128, 0-256)
    if (empty_start < 8) {
        int seg_width = 8 - empty_start;
        if (empty_start + fill_pixels < 8) seg_width = fill_pixels - empty_start;
        render_draw_quad_uv_blended(
            base_x + empty_start, base_y,
            base_x + empty_start + seg_width, bottom,
            empty_start * 16, 0, (empty_start + seg_width) * 16, 256
        );
    }

    // Middle stretch (UV: 128-384, 0-256)
    if (empty_start < (int)(bar_width - 8)) {
        int seg_start = (empty_start > 8) ? empty_start : 8;
        int seg_end = fill_pixels + empty_start;
        if (seg_end > (int)(bar_width - 8)) seg_end = bar_width - 8;
        int seg_width = seg_end - seg_start;
        if (seg_width > 0) {
            render_draw_quad_uv_blended(
                base_x + seg_start, base_y,
                base_x + seg_start + seg_width, bottom,
                128, 0, 384, 256
            );
        }
    }

    // Right cap (UV: offset-based, 0-256)
    int right_start = bar_width - 8;
    if (right_start <= empty_start + fill_pixels) {
        int seg_start = (empty_start > right_start) ? empty_start : right_start;
        int seg_width = (empty_start + fill_pixels) - seg_start;
        render_draw_quad_uv_blended(
            base_x + seg_start, base_y,
            base_x + seg_start + seg_width, bottom,
            ((seg_start - right_start) + 24) * 16, 0,
            ((seg_start + seg_width - right_start) + 24) * 16, 256
        );
    }

    // Draw empty portion (bottom half - standard pass, UV row 256-512)
    // Same 3-segment logic for the unfilled area...
    int empty_pixels = (direction == 0) ? (bar_width - fill_pixels) : 0;
    int empty_offset = (direction == 0) ? fill_pixels : 0;

    // Left cap empty
    if (empty_offset < 8) {
        int seg_width = 8 - empty_offset;
        if (empty_offset + empty_pixels < 8) seg_width = empty_pixels - empty_offset;
        render_draw_quad_uv(
            base_x + empty_offset, base_y,
            base_x + empty_offset + seg_width, bottom,
            empty_offset * 16, 256, (empty_offset + seg_width) * 16, 512
        );
    }

    // Middle empty
    if (empty_offset < (int)(bar_width - 8)) {
        int seg_start = (empty_offset > 8) ? empty_offset : 8;
        int seg_end = empty_offset + empty_pixels;
        if (seg_end > (int)(bar_width - 8)) seg_end = bar_width - 8;
        int seg_width = seg_end - seg_start;
        if (seg_width > 0) {
            render_draw_quad_uv(
                base_x + seg_start, base_y,
                base_x + seg_start + seg_width, bottom,
                128, 256, 384, 512
            );
        }
    }

    // Right cap empty
    if (right_start <= empty_offset + empty_pixels) {
        int seg_start = (empty_offset > right_start) ? empty_offset : right_start;
        int seg_width = (empty_offset + empty_pixels) - seg_start;
        render_draw_quad_uv(
            base_x + seg_start, base_y,
            base_x + seg_start + seg_width, bottom,
            ((seg_start - right_start) + 24) * 16, 256,
            ((seg_start + seg_width - right_start) + 24) * 16, 512
        );
    }

    render_end_blend();
    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e638
// Draws a cached icon at the current position using the icon index table.
// Reads texture dimensions from the descriptor (stride 0x58, width at +0x4E,
// height at +0x52). Draws centered: offset -12,-11 to +39,+39.
// Used for weapon icons, status icons, achievement badges.
// -----------------------------------------------------------------------------
void sprite_draw_icon(int icon_index)
{
    int base_x, base_y;
    u16 tex_width, tex_height;
    int tex_id;
    int desc_offset;

    u64 pos = render_begin_sprites();
    base_x = (int)(pos >> 32);
    base_y = (int)pos;

    // Get texture ID from cached table
    tex_id = g_cached_tex_ids[icon_index];

    // Read dimensions from texture descriptor
    desc_offset = *g_texture_descriptor_base + tex_id * 0x58;
    tex_width = *(u16*)(desc_offset + 0x4E);
    tex_height = *(u16*)(desc_offset + 0x52);

    // Bind and draw
    render_set_texture(tex_id);
    render_draw_quad_uv(
        base_x - 12, base_y - 11,    // top-left (centered offset)
        base_x + 39, base_y + 39,    // bottom-right
        0, 0,                          // UV top-left
        (u32)tex_width << 4,           // UV right (16x scale)
        (u32)tex_height << 4           // UV bottom (16x scale)
    );

    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e6dc
// Draws a UI element sub-sprite from sprite 0x29F using a type selector (0-5).
// The texture atlas contains 6 regions arranged in a 2-row grid:
//   Type 0: (0,0)-(31,31)     - empty checkbox / radio off
//   Type 1: (32,0)-(63,31)    - partial / indeterminate
//   Type 2: (96,0)-(128,31)   - wide element
//   Type 3: (64,0)-(95,31)    - filled checkbox / radio on
//   Type 4: (0,32)-(31,63)    - row 2 variant A
//   Type 5: (32,32)-(63,63)   - row 2 variant B
//
// param_3, param_4: destination width/height
// param_5: type index (0-5)
// Used for checkboxes, radio buttons, toggle indicators.
// -----------------------------------------------------------------------------
void sprite_draw_ui_element(u32 dest_width, u32 dest_height, int type_index)
{
    int base_x, base_y;
    int u0, v0, u1, v1;
    int tex_id;

    u64 pos = render_begin_sprites();
    base_x = (int)(pos >> 32);
    base_y = (int)pos;

    // Bind UI element texture (sprite 0x29F)
    tex_id = sprite_get_texture_id(0x29F);
    render_set_texture(tex_id);

    // Select UV region based on type
    switch (type_index) {
    case 0: u0 = 0x00; v0 = 0x00; u1 = 0x1F; v1 = 0x1F; break;
    case 1: u0 = 0x20; v0 = 0x00; u1 = 0x3F; v1 = 0x1F; break;
    case 2: u0 = 0x60; v0 = 0x00; u1 = 0x80; v1 = 0x1F; break;
    case 3: u0 = 0x40; v0 = 0x00; u1 = 0x5F; v1 = 0x1F; break;
    case 4: u0 = 0x00; v0 = 0x20; u1 = 0x1F; v1 = 0x3F; break;
    case 5: u0 = 0x20; v0 = 0x20; u1 = 0x3F; v1 = 0x3F; break;
    default: goto done;
    }

    render_draw_quad_uv(
        base_x, base_y,
        dest_width, dest_height,
        u0 << 4, v0 << 4,
        u1 << 4, v1 << 4
    );

done:
    render_end_sprites();
}

// Address: 0x8004e810 | Name: sprite_draw_arrow_right | Date: 2026-08-10
// Address: 0x8004e88c | Name: sprite_draw_arrow_left | Date: 2026-08-10
// Address: 0x8004e908 | Name: sprite_draw_arrow_down | Date: 2026-08-10
// Address: 0x8004e984 | Name: sprite_draw_arrow_up | Date: 2026-08-10
// Address: 0x8004ea00 | Name: cursor_get_texture_id | Date: 2026-08-10
// Address: 0x8004ea14 | Name: cursor_get_width | Date: 2026-08-10
// Address: 0x8004ea3c | Name: cursor_get_height | Date: 2026-08-10
// Address: 0x8004ea64 | Name: cursor_draw_at_size | Date: 2026-08-10
// Address: 0x8004eb40 | Name: cursor_draw_stretched | Date: 2026-08-10
// Address: 0x8004ebe4 | Name: sprite_draw_gradient_quad | Date: 2026-08-10

// ============================================================================
// Sprite Drawing Primitives - Arrows, Cursors, Gradients
// ============================================================================

// All 4 arrow functions use sprite 0x29F atlas with different UV regions.
// The atlas is a 128x64 texture containing directional arrows and UI elements.
// UV coordinates are in 16x fixed-point (value << 4 = pixel * 16).
//
// Arrow UV map (in sprite 0x29F):
//   Right arrow: U(0x400-0x5F0) V(0x200-0x3F0) = pixels (64,32)-(95,63)
//   Left arrow:  U(0x5F0-0x400) V(0x200-0x3F0) = mirrored right
//   Down arrow:  U(0x600-0x7F0) V(0x200-0x3F0) = pixels (96,32)-(127,63)
//   Up arrow:    U(0x600-0x7F0) V(0x3F0-0x200) = flipped down

// External: draws a textured quad with UV stretching
extern void render_draw_quad_stretch(f64 x0, f64 y0, f64 x1, f64 y1,
                                     int u0, int v0, int u1, int v1);  // FUN_801708b4

// External: vertex color quad renderer
extern void render_draw_vertex_color_quad(void* vertices, int count, int mode);  // FUN_8016ff24

// External: render state push/pop (lighter weight than full sprite begin/end)
extern f64 render_push_position(void);    // FUN_80382654
extern void render_pop_position(void);    // FUN_803826a0
extern int render_push_cursor_ctx(void);  // FUN_803826f0
extern void render_pop_cursor_ctx(void);  // FUN_8038273c

// -----------------------------------------------------------------------------
// Address: 0x8004e810
// Draws a right-pointing arrow. Uses sprite 0x29F atlas.
// UV region: (0x400,0x200) to (0x5F0,0x3F0).
// Called by hud_draw_fill_gradient, hud_item_draw_value_selector, and menu systems.
// -----------------------------------------------------------------------------
void sprite_draw_arrow_right(f64 x1, f64 y1, f64 x2, f64 y2)
{
    f64 pos = render_push_position();
    int tex_id = sprite_get_texture_id(0x29F);
    render_set_texture(tex_id);
    render_draw_quad_stretch(pos, x1, y1, x2, 0x400, 0x200, 0x5F0, 0x3F0);
    render_pop_position();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e88c
// Draws a left-pointing arrow. Uses sprite 0x29F atlas.
// UV region: (0x5F0,0x200) to (0x400,0x3F0) — horizontally mirrored.
// -----------------------------------------------------------------------------
void sprite_draw_arrow_left(f64 x1, f64 y1, f64 x2, f64 y2)
{
    f64 pos = render_push_position();
    int tex_id = sprite_get_texture_id(0x29F);
    render_set_texture(tex_id);
    render_draw_quad_stretch(pos, x1, y1, x2, 0x5F0, 0x200, 0x400, 0x3F0);
    render_pop_position();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e908
// Draws a down-pointing arrow. Uses sprite 0x29F atlas.
// UV region: (0x600,0x200) to (0x7F0,0x3F0).
// -----------------------------------------------------------------------------
void sprite_draw_arrow_down(f64 x1, f64 y1, f64 x2, f64 y2)
{
    f64 pos = render_push_position();
    int tex_id = sprite_get_texture_id(0x29F);
    render_set_texture(tex_id);
    render_draw_quad_stretch(pos, x1, y1, x2, 0x600, 0x200, 0x7F0, 0x3F0);
    render_pop_position();
}

// -----------------------------------------------------------------------------
// Address: 0x8004e984
// Draws an up-pointing arrow. Uses sprite 0x29F atlas.
// UV region: (0x600,0x3F0) to (0x7F0,0x200) — vertically flipped down arrow.
// -----------------------------------------------------------------------------
void sprite_draw_arrow_up(f64 x1, f64 y1, f64 x2, f64 y2)
{
    f64 pos = render_push_position();
    int tex_id = sprite_get_texture_id(0x29F);
    render_set_texture(tex_id);
    render_draw_quad_stretch(pos, x1, y1, x2, 0x600, 0x3F0, 0x7F0, 0x200);
    render_pop_position();
}

// -----------------------------------------------------------------------------
// Address: 0x8004ea00
// Returns the texture ID for a mapmaker cursor by index (0-74).
// Simple lookup into g_cursor_tex_ids table.
// -----------------------------------------------------------------------------
u32 cursor_get_texture_id(int index)
{
    return g_cursor_tex_ids[index];
}

// -----------------------------------------------------------------------------
// Address: 0x8004ea14
// Returns the pixel width of a mapmaker cursor texture.
// Reads u16 from texture descriptor at (base + tex_id * 0x58 + 0x4E).
// -----------------------------------------------------------------------------
u16 cursor_get_width(int index)
{
    int tex_id = g_cursor_tex_ids[index];
    int desc = *g_texture_descriptor_base + tex_id * 0x58;
    return *(u16*)(desc + 0x4E);
}

// -----------------------------------------------------------------------------
// Address: 0x8004ea3c
// Returns the pixel height of a mapmaker cursor texture.
// Reads u16 from texture descriptor at (base + tex_id * 0x58 + 0x52).
// -----------------------------------------------------------------------------
u16 cursor_get_height(int index)
{
    int tex_id = g_cursor_tex_ids[index];
    int desc = *g_texture_descriptor_base + tex_id * 0x58;
    return *(u16*)(desc + 0x52);
}

// -----------------------------------------------------------------------------
// Address: 0x8004ea64
// Draws a mapmaker cursor at its native texture dimensions.
// Reads width/height from texture descriptor, draws quad at position.
// Full UV mapping (0,0 to width*16, height*16).
// -----------------------------------------------------------------------------
void cursor_draw_at_size(f64 y_pos)
{
    int index = render_push_cursor_ctx();

    int tex_id = g_cursor_tex_ids[index];
    int desc = *g_texture_descriptor_base + tex_id * 0x58;
    u32 width = (u32)*(u16*)(desc + 0x4E);
    u32 height = (u32)*(u16*)(desc + 0x52);

    f64 x_pos = /* from register */;
    render_set_texture(tex_id);
    render_draw_quad_stretch(
        x_pos, y_pos,
        x_pos + (f32)width, y_pos + (f32)height,
        0, 0, width << 4, height << 4
    );

    render_pop_cursor_ctx();
}

// -----------------------------------------------------------------------------
// Address: 0x8004eb40
// Draws a mapmaker cursor stretched to a specified destination rect.
// UV covers full texture, destination rect from params.
// Two render state push/pops: position + cursor context.
// -----------------------------------------------------------------------------
void cursor_draw_stretched(f64 x1, f64 y1, f64 x2, f64 y2)
{
    render_push_position();
    int index = render_push_cursor_ctx();

    int tex_id = g_cursor_tex_ids[index];
    int desc = *g_texture_descriptor_base + tex_id * 0x58;
    u16 tex_width = *(u16*)(desc + 0x4E);
    u16 tex_height = *(u16*)(desc + 0x52);

    f64 pos = /* from register */;
    render_set_texture(tex_id);
    render_draw_quad_stretch(
        pos, x1, y1, x2,
        0, 0, (u32)tex_width << 4, (u32)tex_height << 4
    );

    render_pop_position();
    render_pop_cursor_ctx();
}

// -----------------------------------------------------------------------------
// Address: 0x8004ebe4
// Draws a gradient-colored quad with 4 corner vertices.
// Each vertex has position (x,y) and color (RGBA packed u32).
// Colors: top-left/top-right share param_6 alpha, bottom-left/bottom-right share param_7 alpha.
// RGB base comes from param_5 (upper 24 bits).
//
// Vertex layout (12 bytes each, 4 vertices = 48 bytes):
//   [0] = color (RGB | alpha_top),    pos = (x1, y1)
//   [1] = color (RGB | alpha_bottom), pos = (x2, y1)
//   [2] = color (RGB | alpha_top),    pos = (x1, y2)
//   [3] = color (RGB | alpha_bottom), pos = (x2, y2)
//
// Used for background fills, selection highlight gradients.
// -----------------------------------------------------------------------------
void sprite_draw_gradient_quad(f32 x1, f32 y1, f32 x2, f32 y2,
                               u32 base_color, u32 alpha_top, u32 alpha_bottom)
{
    struct {
        u32 color;
        f32 x;
        f32 y;
    } verts[4];

    u32 color_top = (base_color & 0xFFFFFF00) | (alpha_top & 0xFF);
    u32 color_bot = (base_color & 0xFFFFFF00) | (alpha_bottom & 0xFF);

    verts[0].color = color_top;
    verts[0].x = x1;
    verts[0].y = y1;

    verts[1].color = color_bot;
    verts[1].x = x2;
    verts[1].y = y1;

    verts[2].color = color_top;
    verts[2].x = x1;
    verts[2].y = y2;

    verts[3].color = color_bot;
    verts[3].x = x2;
    verts[3].y = y2;

    render_draw_vertex_color_quad(&verts[0], 4, 0);
}

// Address: 0x8004ec4c | Name: sprite_draw_gradient_quad_alpha | Date: 2026-08-10
// Address: 0x8004ecb8 | Name: sprite_draw_bordered_frame | Date: 2026-08-10
// Address: 0x8004f040 | Name: sprite_measure_text_region | Date: 2026-08-10
// Address: 0x8004f0b4 | Name: sprite_measure_text_region_int | Date: 2026-08-10
// Address: 0x8004f140 | Name: sprite_draw_button_prompt | Date: 2026-08-10
// Address: 0x8004f21c | Name: sprite_draw_button_prompt_alt | Date: 2026-08-10

// ============================================================================
// Sprite Drawing - Frames, Measurement, Button Prompts
// ============================================================================

// External: draw filled rect with blend
extern void render_draw_filled_rect_blend(u32 x0, u32 y0, u32 x1, u32 y1, int blend_mode);
                                                                    // FUN_8016f4bc
// External: draw rotated/transformed quad
extern void render_draw_quad_transformed(/* pos + UV */);           // FUN_80171cbc
// External: text region measurement
extern void sprite_measure_internal(int context, void* out_dims, int mode);  // FUN_8004ee98

// -----------------------------------------------------------------------------
// Address: 0x8004ec4c
// Draws a gradient quad with one corner forced to transparent (alpha=0).
// Same vertex layout as sprite_draw_gradient_quad, but vertex[3].color = 0.
// Creates a fade-to-transparent effect on one edge.
// Used for panel edge fading, vignette overlays.
// -----------------------------------------------------------------------------
void sprite_draw_gradient_quad_alpha(f32 x1, f32 y1, f32 x2, f32 y2,
                                     u32 base_color, u32 alpha_top, u32 alpha_bottom)
{
    struct {
        u32 color;
        f32 x;
        f32 y;
    } verts[4];

    u32 color_top = (base_color & 0xFFFFFF00) | (alpha_top & 0xFF);
    u32 color_bot = (base_color & 0xFFFFFF00) | (alpha_bottom & 0xFF);

    verts[0].color = color_top;
    verts[0].x = x1;
    verts[0].y = y1;

    verts[1].color = color_bot;
    verts[1].x = x2;
    verts[1].y = y1;

    verts[2].color = color_top;
    verts[2].x = x1;
    verts[2].y = y2;

    verts[3].color = 0;  // fully transparent corner
    verts[3].x = x2;
    verts[3].y = y2;

    render_draw_vertex_color_quad(&verts[0], 4, 0);
}

// -----------------------------------------------------------------------------
// Address: 0x8004ecb8
// Draws a 9-slice bordered frame using sprite 0x100.
// The border texture is a 24x24 atlas with 8px corners and stretchable edges.
//
// UV layout (16x fixed-point, original pixels in parentheses):
//   Top-left corner:     (0x000,0x000)-(0x080,0x080)  [0-8, 0-8]
//   Top-right corner:    (0x100,0x000)-(0x180,0x080)  [16-24, 0-8]
//   Bottom-left corner:  (0x000,0x100)-(0x080,0x180)  [0-8, 16-24]
//   Bottom-right corner: (0x100,0x100)-(0x180,0x180)  [16-24, 16-24]
//   Top edge:            (0x080,0x000)-(0x100,0x080)  [8-16, 0-8]
//   Bottom edge:         (0x080,0x100)-(0x100,0x180)  [8-16, 16-24]
//   Left edge:           (0x000,0x080)-(0x080,0x100)  [0-8, 8-16]
//   Right edge:          (0x100,0x080)-(0x180,0x100)  [16-24, 8-16]
//
// Optional fill: if blend_mode != 0, draws solid interior with blending.
// Corner offset is -8px from content rect edges.
// -----------------------------------------------------------------------------
void sprite_draw_bordered_frame(u32 right, u32 bottom, int blend_mode)
{
    u32 left, top;

    u64 pos = render_begin_sprites();
    left = (u32)(pos >> 32);
    top = (u32)pos;

    render_begin_blend();

    // Optional fill interior
    if (blend_mode != 0) {
        render_draw_filled_rect_blend(left, top, right, bottom, blend_mode);
    }

    // Bind border texture
    int tex_id = sprite_get_texture_id(0x100);
    render_set_texture(tex_id);

    // 4 corners (8x8 each)
    render_draw_quad_uv(left - 8, top - 8, left, top,         0x000, 0x000, 0x080, 0x080);  // TL
    render_draw_quad_uv(right,    top - 8, right + 8, top,    0x100, 0x000, 0x180, 0x080);  // TR
    render_draw_quad_uv(left - 8, bottom,  left, bottom + 8,  0x000, 0x100, 0x080, 0x180);  // BL
    render_draw_quad_uv(right,    bottom,  right + 8, bottom + 8, 0x100, 0x100, 0x180, 0x180); // BR

    // 4 edges (stretched)
    render_draw_quad_uv(left, top - 8, right, top,            0x080, 0x000, 0x100, 0x080);  // Top
    render_draw_quad_uv(left, bottom,  right, bottom + 8,     0x080, 0x100, 0x100, 0x180);  // Bottom
    render_draw_quad_uv(left - 8, top, left, bottom,          0x000, 0x080, 0x080, 0x100);  // Left
    render_draw_quad_uv(right, top, right + 8, bottom,        0x100, 0x080, 0x180, 0x100);  // Right

    render_end_blend();
    render_end_sprites();
}

// -----------------------------------------------------------------------------
// Address: 0x8004f040
// Measures a text region and returns float dimensions via out params.
// Wraps FUN_8004ee98 (internal measurement) with sprite context push/pop.
// Output: *out_width, *out_height, *out_line_count (any can be NULL).
// Used for layout calculations before text drawing.
// -----------------------------------------------------------------------------
void sprite_measure_text_region(f32* out_width, f32* out_height, f32* out_lines, int mode)
{
    f32 dims[3];  // [width, height, line_count]

    int ctx = render_push_cursor_ctx();
    sprite_measure_internal(ctx, &dims[0], mode);

    if (out_width != NULL)  *out_width = dims[0];
    if (out_height != NULL) *out_height = dims[1];
    if (out_lines != NULL)  *out_lines = dims[2];

    render_pop_cursor_ctx();
}

// -----------------------------------------------------------------------------
// Address: 0x8004f0b4
// Same as sprite_measure_text_region but casts width/height to int.
// Output: *out_width (int), *out_height (int), *out_lines (as-is).
// Used when pixel-exact integer layout is needed.
// -----------------------------------------------------------------------------
void sprite_measure_text_region_int(int* out_width, int* out_height, f32* out_lines, int mode)
{
    f32 dims[3];

    int ctx = render_push_cursor_ctx();
    sprite_measure_internal(ctx, &dims[0], mode);

    if (out_width != NULL)  *out_width = (int)dims[0];
    if (out_height != NULL) *out_height = (int)dims[1];
    if (out_lines != NULL)  *out_lines = dims[2];

    render_pop_cursor_ctx();
}

// -----------------------------------------------------------------------------
// Address: 0x8004f140
// Draws a controller button prompt icon using sprite 0x10F.
// Rendered at native texture size with rotation support.
// Uses render_draw_quad_transformed for angle parameter.
// Called by hud_draw_panel_frame for on-screen "Press A" style prompts.
// -----------------------------------------------------------------------------
void sprite_draw_button_prompt(f64 y0, f64 x1, f64 y1)
{
    int tex_id;
    int desc_offset;
    u16 tex_width, tex_height;

    u64 ctx = render_push_cursor_ctx();

    tex_id = sprite_get_texture_id(0x10F);
    desc_offset = *g_texture_descriptor_base + tex_id * 0x58;
    tex_width = *(u16*)(desc_offset + 0x4E);
    tex_height = *(u16*)(desc_offset + 0x52);

    render_set_texture(tex_id);
    render_draw_quad_transformed(
        ctx, y0, x1, y1,
        0.0f,  // rotation angle
        0, 0, (u32)tex_width << 4, (u32)tex_height << 4
    );

    render_pop_cursor_ctx();
}

// -----------------------------------------------------------------------------
// Address: 0x8004f21c
// Draws a controller button prompt icon (alternate render state path).
// Identical to sprite_draw_button_prompt but uses different sprite context
// (render_begin_sprites variant for different blend/depth state).
// -----------------------------------------------------------------------------
void sprite_draw_button_prompt_alt(f64 y0, f64 x1, f64 y1)
{
    int tex_id;
    int desc_offset;
    u16 tex_width, tex_height;

    u64 ctx = render_begin_sprites();  // alternate path

    tex_id = sprite_get_texture_id(0x10F);
    desc_offset = *g_texture_descriptor_base + tex_id * 0x58;
    tex_width = *(u16*)(desc_offset + 0x4E);
    tex_height = *(u16*)(desc_offset + 0x52);

    render_set_texture(tex_id);
    render_draw_quad_transformed(
        ctx, y0, x1, y1,
        0.0f,
        0, 0, (u32)tex_width << 4, (u32)tex_height << 4
    );

    render_end_sprites();  // alternate path
}
