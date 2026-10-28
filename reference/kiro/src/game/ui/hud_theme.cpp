// HUD Theme/Style System
// Style initialization, linked-list lookups for elements/fonts/colors/spacings
// Batch 094 | Date: 2026-08-10

#include "common.h"
#include "hud.h"

// Style system globals (linked lists for theme resources)
static int g_style_element_list;   // DAT_80611a24
static int g_style_font_list;      // DAT_80611a1c
static int g_style_color_list;     // DAT_80611a14
static int g_style_spacing_list;   // DAT_80611a0c
static int g_active_theme;         // DAT_80611a44

// ============================================================================
// Address: 0x8004cdf0 | Name: hud_theme_init_default
// Initializes the default HUD theme/style system. Zeroes 12 style globals,
// configures fonts, colors (5 palettes with RGBA), spacings, padding, focus
// pulse parameters. Called by hud_alloc_element_pool during startup.
// ============================================================================
void hud_theme_init_default(/* many params via registers */) {
    // Zero all 12 style state globals
    DAT_80611a34 = 0;
    DAT_80611a30 = 0;
    DAT_80611a2c = 0;
    DAT_80611a28 = 0;
    DAT_80611a24 = 0;
    DAT_80611a20 = 0;
    DAT_80611a1c = 0;
    DAT_80611a18 = 0;
    DAT_80611a14 = 0;
    DAT_80611a10 = 0;
    DAT_80611a0c = 0;
    DAT_80611a08 = 0;

    // Configure base style (fonts, layout params passed as -1 defaults)
    FUN_8004d334(/* params */, -1, -1 /*, ... */);

    // Set drawing mode
    FUN_8004d3e0(0, 1);

    // Configure spacing (padding top/bottom)
    FUN_8004d418((double)FLOAT_80613d40, (double)FLOAT_80613d40);
    FUN_8004d440((double)FLOAT_80613d40, (double)FLOAT_80613d40);

    // Configure focus pulse (color, alpha, speed, amplitude)
    float pulse_color = FLOAT_80613d44;
    FUN_8004d454((double)pulse_color, (double)pulse_color,
                 (double)pulse_color, (double)FLOAT_80613d48);

    // Configure font IDs (normal=0x12, bold=0x15, header=0x16)
    FUN_8004d478(0x12, 0x15, 0x16);

    // Configure item spacing and margins
    float margin = FLOAT_80613d50;
    float pad = FLOAT_80613d40;
    FUN_8004d4cc((double)FLOAT_80613d4c, (double)margin, (double)margin,
                 (double)pad, (double)pad);

    // Configure separator style
    FUN_8004d4f8((double)FLOAT_80613d54, (double)FLOAT_80613d44, 1, 0);

    // Configure sub-style A
    FUN_8004d5bc(/* inherited params */);

    // Configure sub-style B (all same padding)
    FUN_8004d65c((double)pad, (double)pad, (double)pad, (double)pad);
    FUN_8004d5a4((double)FLOAT_80613d44);
    FUN_8004d5b0((double)FLOAT_80613d58);
    FUN_8004d680(/* inherited params */);

    // Configure 5 color palettes (RGBA packed u32)
    FUN_8004d720(0, 0x6e6e6e7f, 0x1414147f);  // Default: grey / dark
    FUN_8004d720(1, 0x78787f,   0x507f);       // Focused: blue tint
    FUN_8004d720(2, 0x7f7f7f7f, 0x40);         // Selected: white / dim
    FUN_8004d720(3, 0x46464650, 0x40);          // Disabled: dark grey
    FUN_8004d720(4, 0x46464650, 0x40);          // Locked: dark grey

    // Configure gradient stops (4 levels)
    FUN_8004d73c(0, 0x1e2d4646);
    FUN_8004d73c(1, 0x2d4b697f);
    FUN_8004d73c(2, 0x69737f7f);
    FUN_8004d73c(3, 0x1e283237);

    // Configure shadow/outline
    FUN_8004d7d8(0x1e235a00, 0, 0);

    // Finalize and apply
    FUN_8004d58c(0);
    FUN_8004d240(/* inherited params */);
    FUN_8004d2e0(-1, -1, -1);
    FUN_8004d328(-1);

    // Reset counters
    hud_style_reset_counters();

    DAT_80611a48 = 0;
    FUN_8004d098(-1);
}

// ============================================================================
// Address: 0x8004cff8 | Name: hud_style_reset_counters
// Resets 4 style state counters to zero. Called during theme init and resets.
// ============================================================================
void hud_style_reset_counters(void) {
    DAT_80611a34 = 0;
    DAT_80611a30 = 0;
    DAT_80611a2c = 0;
    DAT_80611a28 = 0;
}

// ============================================================================
// Address: 0x8004d010 | Name: hud_style_find_element_by_id
// Finds a style element descriptor by ID. Walks linked list at DAT_80611a24,
// comparing param against +0x48 (ID field). Returns matching node pointer.
// ============================================================================
int hud_style_find_element_by_id(int id) {
    int node = g_style_element_list;  // DAT_80611a24
    while (id != *(int*)(node + 0x48)) {
        node = *(int*)(node + 0x4c);  // next pointer
    }
    return node;
}

// ============================================================================
// Address: 0x8004d030 | Name: hud_style_find_font_by_id
// Finds a font descriptor by ID. Walks linked list at DAT_80611a1c,
// comparing param against +0xDC (ID field). Returns matching node pointer.
// ============================================================================
int hud_style_find_font_by_id(int id) {
    int node = g_style_font_list;  // DAT_80611a1c
    while (id != *(int*)(node + 0xdc)) {
        node = *(int*)(node + 0xe0);  // next pointer
    }
    return node;
}

// ============================================================================
// Address: 0x8004d050 | Name: hud_style_find_color_by_id
// Finds a color palette entry by ID. Walks linked list at DAT_80611a14,
// comparing param against +0x10 (ID field). Returns matching node pointer.
// ============================================================================
int hud_style_find_color_by_id(int id) {
    int node = g_style_color_list;  // DAT_80611a14
    while (id != *(int*)(node + 0x10)) {
        node = *(int*)(node + 0x14);  // next pointer
    }
    return node;
}

// ============================================================================
// Address: 0x8004d070 | Name: hud_style_find_spacing_by_id
// Finds a spacing/layout descriptor by ID. Walks linked list at DAT_80611a0c,
// comparing param against +0x90 (ID field). Returns matching node pointer.
// ============================================================================
int hud_style_find_spacing_by_id(int id) {
    int node = g_style_spacing_list;  // DAT_80611a0c
    while (id != *(int*)(node + 0x90)) {
        node = *(int*)(node + 0x94);  // next pointer
    }
    return node;
}

// ============================================================================
// Address: 0x8004d090 | Name: hud_style_get_active_theme
// Returns the currently active HUD theme ID. Used by element init/finalize
// to select theme-appropriate styles.
// ============================================================================
int hud_style_get_active_theme(void) {
    return g_active_theme;  // DAT_80611a44
}
