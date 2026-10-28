// =============================================================================
// boot_splash.cpp - Boot splash screen / startup sequence
// TimeSplitters: Future Perfect (GameCube) Decompilation
// Batch 060 | Date: 2026-08-05
// =============================================================================

#include "common.h"
#include "input.h"
#include "music.h"
#include "resource.h"
#include "ui.h"

// Forward declarations
extern void  preload_string_tables(void* params);
extern void  load_story_string_table(void);
extern int   FUN_80180434(void);  // get disc/hardware state
extern int   FUN_8004d850(int sprite_id);
extern void  FUN_8016e77c(int sprite_handle);
extern void  FUN_8016f00c(int mode);
extern void  FUN_80170410(int sprite, int x, int y, int w, int h, int u, int v, int tex_w, int tex_h);
extern void  FUN_8016f4bc(int sprite, int x, int y, int h, int alpha);
extern void  FUN_801a6f00(char* out_buf);
extern u16   award_get_name(int index, int type);
extern void  match_start_cutscene_transition(u32 param, int p2, ...);

// Boot splash state
static int   g_splash_counter;       // DAT_806119b0
static int   g_splash_state;         // DAT_806119b4 - states: 0=inactive, 1=language select, 2=delay, 3=queue movies, 5=wait+transition
static u32   g_splash_selection;     // DAT_806119ac - currently selected language/option
static int   g_splash_skip_flag;     // DAT_806119a8 - if set, skip splash to menu
static int   g_disc_state;           // DAT_8060f948
static int   g_skip_movies;          // DAT_80612240

// Rendering data
extern int*  DAT_80611748;   // sprite data array
extern int*  DAT_80610a20;   // sprite table base

// Timing
extern int   DAT_806109c0;   // frame rate multiplier

// Constants
static const float FADE_PERCENT_LEFT  = 0.0f;  // FLOAT_80613b80
static const float FADE_PERCENT_RIGHT = 0.0f;  // FLOAT_80613b84

// Movie paths
static const char* MOVIE_EA  = "movies/ea.thp";
static const char* MOVIE_FRD = "movies/frd.thp";

// -----------------------------------------------------------------------------
// Address: 0x8003bc88 | Name: boot_stub_return_zero
// -----------------------------------------------------------------------------
// Stub function - always returns 0. Part of boot splash screen function table.
// -----------------------------------------------------------------------------
int boot_stub_return_zero(void) {
    return 0;
}

// -----------------------------------------------------------------------------
// Address: 0x8003bc90 | Name: boot_nop
// -----------------------------------------------------------------------------
// Empty function (no-op). Part of boot splash screen function table.
// -----------------------------------------------------------------------------
void boot_nop(void) {
    // intentionally empty
}

// -----------------------------------------------------------------------------
// Address: 0x8003bc94 | Name: boot_splash_init_state
// -----------------------------------------------------------------------------
// Initializes boot splash state. Sets counter to 0, sets state to 3
// (play movies) unless skip flag is set (jump to menu, state=1).
// -----------------------------------------------------------------------------
void boot_splash_init_state(void) {
    g_splash_counter = 0;
    g_splash_state = 3;  // default: queue movies

    if (g_splash_skip_flag != 0) {
        g_splash_state = 1;  // skip to language select
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003bcb8 | Name: boot_splash_setup
// -----------------------------------------------------------------------------
// Boot splash setup. Reads hardware/disc state via FUN_80180434 to determine
// splash screen mode. Maps disc state to internal state enum.
// Falls through to boot_splash_init_state.
// -----------------------------------------------------------------------------
void boot_splash_setup(void) {
    g_splash_counter = 0;
    g_disc_state = 0;

    if (g_skip_movies == 0) {
        int hw_state = FUN_80180434();

        switch (hw_state) {
            case 0:
            case 5:
                g_disc_state = 0;  // normal boot
                break;
            case 1:
                g_disc_state = 1;  // disc error type 1
                break;
            case 2:
                g_disc_state = 3;  // disc error type 2
                break;
            case 3:
                g_disc_state = 2;  // disc error type 3
                break;
            case 4:
                g_disc_state = 4;  // fatal disc error
                break;
            default:
                g_splash_skip_flag = 1;  // unknown state - skip splash
                break;
        }
    }

    boot_splash_init_state();
}

// -----------------------------------------------------------------------------
// Address: 0x8003bd58 | Name: boot_preload_strings
// -----------------------------------------------------------------------------
// Wrapper that calls preload_string_tables for boot sequence.
// Passes through all parameters from the game state system.
// -----------------------------------------------------------------------------
void boot_preload_strings(void* params) {
    preload_string_tables(params);
}

// -----------------------------------------------------------------------------
// Address: 0x8003bd78 | Name: boot_splash_update
// -----------------------------------------------------------------------------
// Main boot splash screen update loop. Handles states:
//   1 = language/region select (D-pad input)
//   2 = delay timer (waits for N frames)
//   3 = queue EA/FRD logo movies for playback
//   5 = wait 10 frames then transition to main game
// Calls music_set_volume, input_get_buttons_held, game_resource_loader for
// movie playback, and match_start_cutscene_transition to enter main menu.
// -----------------------------------------------------------------------------
void boot_splash_update(void) {
    music_set_volume(0.0f);  // mute during splash

    // Setup text rendering region
    FUN_80133400(60, 250, 392, 15);
    FUN_801333d4(0.0f);

    // Load story strings if disc state is valid
    if (g_disc_state != -1) {
        load_story_string_table();
    }

    // State machine
    if (g_splash_state == 1) {
        // Language/region selection with D-pad
        u32 buttons = input_get_buttons_held(0);

        u32 selection = g_splash_selection;

        if (buttons & 0x04) {
            // D-pad Up
            g_splash_counter = 0;
            selection = 4;
            if ((int)g_splash_selection > 0) {
                selection = g_splash_selection - 1;
            }
        }
        else if (buttons & 0x08) {
            // D-pad Down
            g_splash_counter = 0;
            selection = g_splash_selection + 1;
            if (g_splash_selection >= 4) {
                selection = 0;
            }
        }
        else if (buttons & 0x1F0) {
            // Any face button - confirm selection
            g_splash_counter = 0;
            g_disc_state = g_splash_selection;
            load_story_string_table();
            g_splash_state = 3;
            selection = g_splash_selection;
        }

        g_splash_selection = selection;
        input_sort_controllers();
    }
    else if (g_splash_state == 2) {
        // Delay timer state
        g_splash_counter++;
        if (g_splash_counter > DAT_806109c0 * 3) {
            g_splash_counter = 0;
            g_splash_state = 3;  // advance to movie playback
        }
    }
    else if (g_splash_state == 3) {
        // Queue logo movies
        game_resource_loader_21bc("movies/ea.thp", 0, /*...*/);
        game_resource_loader_21bc("movies/frd.thp", 0, /*...*/);
        g_splash_state = 5;  // wait for movies to finish
    }

    // Post-movie transition
    if (g_splash_state == 5) {
        g_splash_counter++;
        if (g_splash_counter == 10) {
            char info_buf[20];
            char controllers[56];
            char controller_count;

            FUN_801a6f00(info_buf);

            // Map controllers to player slots
            for (int i = 0; i < info_buf[0]; i++) {
                controllers[i] = (char)award_get_name(i, (int)controller_count);
            }

            // Transition to main menu
            match_start_cutscene_transition(/*...*/);
        }
    }
}

// -----------------------------------------------------------------------------
// Address: 0x8003bf54 | Name: boot_splash_render_logo
// -----------------------------------------------------------------------------
// Renders boot splash logo/overlay. Looks up sprite 0x317, sets up draw region,
// applies alpha fade effects on left and right edges.
// -----------------------------------------------------------------------------
void boot_splash_render_logo(void) {
    int sprite_handle = FUN_8004d850(0x317);  // EA/FRD logo sprite ID

    FUN_8016e77c(sprite_handle);  // bind sprite
    FUN_8016f00c(1);              // set draw mode

    // Get sprite dimensions from table
    int sprite_entry = *DAT_80610a20 + sprite_handle * 0x58;
    u16 tex_w = *(u16*)(sprite_entry + 0x4E);
    u16 tex_h = *(u16*)(sprite_entry + 0x52);

    // Draw full sprite at viewport position
    FUN_80170410(
        *DAT_80611748,           // sprite ref
        DAT_80611748[5],         // x position
        DAT_80611748[1] + 1,     // y position
        DAT_80611748[6] + 1,     // width
        0, 0,                    // UV offset
        (int)tex_w, (int)tex_h   // texture size
    );

    // Apply alpha fade on left edge
    int height = DAT_80611748[8];
    float fade_height_left = FADE_PERCENT_LEFT * (float)height;
    FUN_8016f4bc(
        *DAT_80611748,
        DAT_80611748[5],
        DAT_80611748[1] + 1,
        (int)fade_height_left,
        0x7F  // 50% alpha
    );

    // Apply alpha fade on right edge
    float fade_height_right = FADE_PERCENT_RIGHT * (float)height;
    FUN_8016f4bc(
        *DAT_80611748,
        (int)fade_height_right,
        DAT_80611748[1] + 1,
        DAT_80611748[6] + 1,
        0x7F  // 50% alpha
    );
}

// -----------------------------------------------------------------------------
// Address: 0x8003c080 | Name: boot_splash_render
// -----------------------------------------------------------------------------
// Renders the boot splash screen. Two modes:
//   State 1: Language selection UI - draws 5 flag sprites with text labels
//            (ENGLISH/FRANCAIS/DEUTSCH/ESPANOL/ITALIANO), highlighted selection
//            with pulsing glow effect.
//   State 0: Disc error/info text overlay - centered multi-line messages
//            using get_string_by_id(0x1989) or disc-state-specific strings.
// Calls boot_splash_render_logo for the background logo sprite.
// -----------------------------------------------------------------------------

// Language names table at 0x803fdf2c
static const char* g_language_names[] = {
    "ENGLISH",
    "FRANCAIS",
    "DEUTSCH",
    "ESPANOL",
    "ITALIANO"
};

// Flag sprite IDs at 0x803fdf40
static int g_flag_sprite_ids[5];

// Disc error message pointers at 0x803fdf00
extern void* PTR_DAT_803fdf00[];

void boot_splash_render(void) {
    FUN_803826b4();  // enter render section

    // Setup rendering state
    FUN_8001c6c8(DAT_80611d54 + 0x224);
    FUN_8001c8ec();

    // Clear screen with dark overlay
    FUN_8016f4bc(0, 0, DAT_806109c4, DAT_806109c8, 0x7F);

    if (g_splash_state == 1) {
        // === Language Selection Mode ===
        int prev_font = text_set_font_table(0x0B);  // flag font

        int line_height = (int)FLOAT_80611674;
        int screen_h = *(int*)(DAT_80611748 + 0x20);
        int screen_w = *(int*)(DAT_80611748 + 0x1C);
        int flag_width = (int)(0.0f /* FLOAT_80613b90 */ * (float)screen_h);
        u32 total_height = line_height * 5 + flag_width * 4;

        boot_splash_render_logo();
        FUN_8016f00c(1);
        FUN_8004e034();  // begin sprite batch

        u8 alpha_pulse = DAT_80611a51;

        for (int i = 0; i < 5; i++) {
            int text_x = /* calculated from screen_w and font metrics */;
            int y_pos = (screen_w - (total_height / 2)) + i * (line_height + flag_width);

            int flag_sprite = FUN_8004d850(g_flag_sprite_ids[i]);

            if (i == g_splash_selection) {
                // Draw highlight glow (4 corners with pulsing alpha)
                u32 glow_color = (alpha_pulse & 0x1FE) << 15 | 0x8080;
                FUN_8004ec4c(text_x, y_pos + 0x14, text_x - 0x80, y_pos, glow_color, 0x80, 0);
                FUN_8004ec4c(text_x, y_pos + 0x14, text_x - 0x80, y_pos + 0x28, glow_color, 0x80, 0);
                FUN_8004ec4c(text_x, y_pos + 0x14, text_x + 0x80, y_pos, glow_color, 0x80, 0);
                FUN_8004ec4c(text_x, y_pos + 0x14, text_x + 0x80, y_pos + 0x28, glow_color, 0x80, 0);

                // Draw selected text with shadow
                text_draw_char_shadowed(
                    (float)text_x, (float)y_pos,
                    g_language_names[i],
                    alpha_pulse << 24 | 0x787880,
                    (alpha_pulse + 0x28) * 0x80 & 0xFF00 | 0x80
                );
            } else {
                // Draw unselected text (gray)
                text_draw_at_int_coords(
                    &text_x, &y_pos,
                    g_language_names[i],
                    0x50505080
                );
            }

            // Draw flag sprite
            FUN_8016e77c(flag_sprite);
            int sprite_entry = *DAT_80610a20 + flag_sprite * 0x58;
            u16 tex_w = *(u16*)(sprite_entry + 0x4E);
            u16 tex_h = *(u16*)(sprite_entry + 0x52);
            FUN_80170410(/* x, y, w, h, u, v, tex_w*16, tex_h*16 */);
        }

        text_set_font_table(prev_font);
    }
    else if (g_splash_state == 0 && DAT_806109c0 < g_splash_counter) {
        // === Disc Error / Info Text Mode ===
        bool show_all_messages = (g_skip_movies == 0 && g_splash_skip_flag != 0);
        int num_lines = show_all_messages ? 5 : 1;

        float text_scale = FLOAT_80613ba8 * (float)DAT_806109c4;
        text_set_font_table(0x12);

        boot_splash_render_logo();
        DAT_80611654 = 1;  // enable centered text mode

        // First pass: measure total text height
        u32 total_text_h = 0;
        for (int i = 0; i < num_lines; i++) {
            char* msg;
            if (i == 0 && g_skip_movies != 0) {
                msg = (char*)get_string_by_id(0x1989);
            } else if (i == 0 && g_splash_skip_flag == 0) {
                msg = (char*)PTR_DAT_803fdf00[g_disc_state];
            } else {
                msg = (char*)PTR_DAT_803fdf00[i];
            }

            char* scaled = text_check_scale(text_scale, msg);
            float line_h = text_get_char_height(scaled);
            total_text_h += (u32)line_h;

            if (i != num_lines - 1) {
                // Add line spacing
                total_text_h += (u32)(FLOAT_80613b90 * (float)screen_h);
            }
        }

        // Second pass: draw centered
        u32 y_start = (DAT_806109c8 - total_text_h) / 2;
        for (int i = 0; i < num_lines; i++) {
            char* msg = /* same selection as above */;
            char* scaled = text_check_scale(text_scale, msg);
            int center_x = DAT_806109c4 / 2;

            text_draw_char((float)center_x, (float)y_start, scaled, 0x7F7F7F7F);

            float line_h = text_get_char_height(scaled);
            y_start += (u32)line_h;
            y_start += (u32)(FLOAT_80613b90 * (float)screen_h);
        }

        DAT_80611654 = 0;
    }

    FUN_80382700();  // leave render section
}
