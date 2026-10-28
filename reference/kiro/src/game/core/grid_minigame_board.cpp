// ============================================================================
// Address range: 0x80366490 - 0x8036723c | batch_1030 | 2026-09-12
//
// Ekun grid minigame board - the arcade bonus grid reached from the mode-grid2
// menu and the ekun confirm menu. This module picks the match configuration for
// the grid mode, reads the analog-stick navigation, loads a board layout (a
// fixed default or one of three RNG-selected templates) into the live board
// state block at 0x805ec314, spawns an entity per grid cell (tile types
// 0x337/0x338/0x339 by cell value, plus 0x336 border tiles) and renders the
// grid lines through the GX text pipeline.
//
//   grid_minigame_select_config      0x80366490  pick match config for the grid mode
//   grid_minigame_read_input         0x80366700  analog-stick nav -> direction bitmask
//   grid_minigame_load_default_board 0x80366844  copy the fixed default board
//   grid_minigame_init_random_board  0x803668a8  pick+load an RNG board, build it
//   grid_minigame_load_board         0x80366970  copy a numbered board template
//   grid_minigame_tick_fwd           0x803669b8  tick trampoline
//   grid_minigame_reset_fwd          0x803669dc  reset trampoline
//   grid_minigame_render_lines       0x803669fc  render the grid lines (GX)
//   grid_minigame_spawn_border_tile  0x80366dc8  spawn one 0x336 border tile
//   grid_minigame_build_board        0x8036723c  spawn all cell + border tiles
//
// NOTE: non-matching decomp. The board state block layout (rows DAT_805ec318,
// cols DAT_805ec31a, cell array at +0x14 stride 0x8c, border-run list at
// DAT_805ec6f0..) is reproduced from the original offsets. The heavy paired-
// single / CONCAT44 float coordinate arithmetic in the two spawn/render
// builders is condensed to equivalent float expressions - the control flow,
// entity types, and side effects match the original; the exact per-vertex
// coordinate rounding is not bit-reproduced. Several engine callees whose
// generated prototypes were stripped are invoked through typed function-pointer
// casts.

#include "common.h"
#include "generated_decls.h"

// ---- board state block (0x805ec314 ..) ------------------------------------
extern unsigned int  DAT_805ec318;   // grid row count
extern unsigned int  DAT_805ec31a;   // grid column count
extern int   DAT_805ec6f0;           // border-run count
extern char  DAT_805ec6f4;           // border run A: coord pairs (stride 2 bytes at +2 groups)
extern char  DAT_805ec6f5;
extern char  DAT_805ec6fc;           // border run B
extern char  DAT_805ec6fd;
extern float DAT_805ec704;           // border rotation matrix scratch

// ---- match-config output fields (0x805cc6xx) ------------------------------
extern int   DAT_805cc614;
extern int   DAT_805cc634;
extern unsigned char DAT_805cc68c, DAT_805cc68d, DAT_805cc68e;
extern int   DAT_805cc698, DAT_805cc69c, DAT_805cc6a0, DAT_805cc6ac;

// ---- config template tables -----------------------------------------------
extern int   DAT_804942f0;   // grid-mode config row table (stride 0x14)
extern int   DAT_803f2dd0;   // mode descriptor table (stride 0x20)
extern int   DAT_803f31e4;   // mode class table (stride 0xf)
extern int   DAT_806109c0;   // frame counter (for the time-limit calc)

// ---- input edge-detect latches --------------------------------------------
extern int   DAT_806112dc;   // vertical-axis re-arm latch
extern int   DAT_806112e0;   // horizontal-axis re-arm latch

// ---- board init/reset scratch state ---------------------------------------
extern int   DAT_80612ea0, DAT_80612ea4, DAT_80612ea8, DAT_80612eac;
extern int   uRam80612eb0;
extern float FLOAT_80612eb4;

// ---- rodata float constants ------------------------------------------------
extern float FLOAT_8061b260, FLOAT_8061b264, FLOAT_8061b268;   // rng->row scale
extern double DOUBLE_8061b270;
extern float FLOAT_8061b278, FLOAT_8061b27c, FLOAT_8061b280;   // input deadzone/thresholds
extern double DOUBLE_8061b288, DOUBLE_8061b2b8;
extern float FLOAT_8061b284, FLOAT_8061b290, FLOAT_8061b294, FLOAT_8061b298, FLOAT_8061b29c;
extern float FLOAT_8061b2a0, FLOAT_8061b2a4, FLOAT_8061b2a8, FLOAT_8061b2ac, FLOAT_8061b2b0, FLOAT_8061b2b4;
extern float DAT_803e0c38, DAT_803e0c40, DAT_803e0c44, DAT_803e0c4c;
extern int   DAT_803e0c3c, DAT_803e0c48;

// ---- board template source bases ------------------------------------------
#define BOARD_LIVE      0x805ec314               // live board state block base
#define BOARD_DEFAULT   0x80494894               // fixed default template

// scratch-scope helpers.
extern unsigned long long FUN_803826f0(void);   // scratch enter (spawn: owner/coord pair)
extern int  FUN_803826c0(void);                  // scratch enter (build: instance ptr)
extern int  FUN_803826cc(void);                  // scratch enter (render: y-origin)
extern void FUN_8038273c(void);                  // scratch leave
extern void FUN_80382718(void);                  // scratch leave
extern void FUN_8038270c(void);                  // scratch leave

// Grid tail helpers (defined at the end of this file, batch_1032). Forward
// declarations so the batch_1030/1031 call sites above resolve.
void grid_minigame_draw_border_seg(double a1, double a2, double a3);       // 0x80368d7c
void grid_minigame_wall_face_spark(int dir, int inst, int idx, int faceTab, int matId, int cell); // 0x80369004
#define FUN_80368d7c grid_minigame_draw_border_seg
#define FUN_80369004 grid_minigame_wall_face_spark

// engine callees with stripped generated prototypes -> typed casts.
typedef unsigned int (*RngTickFn)(void);
typedef int (*SpawnActFn)(double, double, double, unsigned long long, unsigned long long,
                          unsigned long long, unsigned long long, unsigned long long,
                          int, unsigned int, float*, float*, int, int);

// The three RNG board templates live at 0x804944a8 + idx*0x3ec (idx 1,3,4).
static inline const int* board_template(int idx)
{
    return (const int*)(idx * 0x3ec + (int)0x804944a8);
}

// Copy a 0x7d-pair (+ tail) board template into the live board block. Mirrors
// the original's word-pair copy loop.
static void copy_board(const int* src)
{
    int* dst = (int*)BOARD_LIVE;
    for (int i = 0x7d; i != 0; i--) {
        dst[1] = src[1];
        dst[2] = src[2];
        dst += 2;
        src += 2;
    }
    dst[3] = src[3];
}

// -------------------------------------------------------------------------
// 0x80366490  grid_minigame_select_config - pick match config for the grid mode.
//   Draws an RNG value to choose a row of the grid-mode config table
//   (DAT_804942f0, stride 0x14), then fills the match-setup fields and the
//   game-settings block from that row (mode id, class, time limit, and the
//   per-mode value/story/time-threshold indices), honouring the row's option
//   flag bits, and rebuilds the arcade selection state.
// -------------------------------------------------------------------------
void grid_minigame_select_config(void)
{
    FUN_803826f0();
    unsigned int rng = ((RngTickFn)&rng_counter_tick)();
    float pick = FLOAT_8061b264 *
        ((float)((double)rng - DOUBLE_8061b270) / FLOAT_8061b268) + FLOAT_8061b260;

    DAT_805cc614 = (match_is_objective_mode() == 0) ? 0xd : 0xc;

    int row = (int)pick * 0x14;
    DAT_805cc634 = *(int*)(&DAT_804942f0 + row);
    unsigned int modeId = *(unsigned int*)((int)&DAT_804942f0 + row + 4);
    DAT_805cc68e = (unsigned char)modeId;
    DAT_805cc68d = (unsigned char)*(int*)(&DAT_803f2dd0 + (modeId & 0xff) * 0x20);
    DAT_805cc68c = (unsigned char)(&DAT_803f31e4)[DAT_805cc634 * 0xf];

    char* settings = (char*)game_get_settings_ptr();
    DAT_805cc69c = 0;
    DAT_805cc698 = DAT_806109c0 * *(int*)((int)&DAT_804942f0 + row + 8) * 0x3c;
    DAT_805cc6a0 = 0;
    DAT_805cc6ac = 0;

    int* modeSlot = (int*)(settings + (unsigned int)DAT_805cc68d * 0x14 + 0x24);
    int rowValue = *(int*)((int)&DAT_804942f0 + row + 0xc);
    switch (DAT_805cc68e) {
        case 3: case 4: case 8: case 10: case 0x12:
            DAT_805cc69c = rowValue;
            break;
        case 5:
            DAT_805cc6a0 = rowValue;
            break;
        case 9:
            *(int*)(settings + 0x6ac) = 1;
            // fallthrough
        case 7:
            DAT_805cc6ac = rowValue;
            modeSlot[4] = mode_value_index_lookup();
            break;
    }

    modeSlot[3] = mode_story_index_lookup();
    modeSlot[2] = mode_time_threshold_index();

    unsigned short flags = *(unsigned short*)((int)&DAT_804942f0 + row + 0x10);
    modeSlot[0] = (flags & 1) ? 1 : 0;
    modeSlot[1] = (flags & 2) ? 1 : 0;
    *(int*)(settings + 0x20) = (flags & 4) ? 1 : 0;
    *(int*)(settings + 0x18) = (flags & 8) ? 2 : 1;
    *(int*)(settings + 8)    = (flags & 0x10) ? 0 : 1;

    arcsel_state_build();
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x80366700  grid_minigame_read_input - analog-stick nav -> direction bitmask.
//   Edge-triggers on the vertical (axis 2) and horizontal (axis 1) sticks past
//   a deadzone, re-arming when the stick returns near centre, and ORs in the
//   currently held buttons. Bits: up 8, down 4, left 1, right 2.
// -------------------------------------------------------------------------
unsigned int grid_minigame_read_input(int pad)
{
    unsigned int dir = 0;

    double v = input_get_analog_value(pad, 2);
    if (((double)FLOAT_8061b278 < ABS(v)) && (DAT_806112dc != 0)) {
        v = input_get_analog_value(pad, 2);
        dir = (v <= (double)FLOAT_8061b27c) ? 8u : 4u;
        DAT_806112dc = 0;
    }
    v = input_get_analog_value(pad, 2);
    if ((ABS(v) < (double)FLOAT_8061b280) && (DAT_806112dc == 0)) {
        DAT_806112dc = 1;
    }

    double h = input_get_analog_value(pad, 1);
    if (((double)FLOAT_8061b278 < ABS(h)) && (DAT_806112e0 != 0)) {
        h = input_get_analog_value(pad, 1);
        dir |= (h <= (double)FLOAT_8061b27c) ? 1u : 2u;
        DAT_806112e0 = 0;
    }
    h = input_get_analog_value(pad, 1);
    if ((ABS(h) < (double)FLOAT_8061b280) && (DAT_806112e0 == 0)) {
        DAT_806112e0 = 1;
    }

    return dir | input_get_buttons_held(pad);
}

// -------------------------------------------------------------------------
// 0x80366844  grid_minigame_load_default_board - copy the fixed default board.
// -------------------------------------------------------------------------
void grid_minigame_load_default_board(void)
{
    const int* src = (const int*)BOARD_DEFAULT;
    int* dst = (int*)BOARD_LIVE;
    for (int i = 0x7d; i != 0; i--) {
        dst[1] = src[1];
        dst[2] = src[2];
        FLOAT_80612eb4 = FLOAT_8061b27c;
        dst += 2;
        src += 2;
    }
    dst[3] = src[3];
    DAT_80612eac = 0;
    uRam80612eb0 = 0;
    DAT_80612ea8 = 0;
    DAT_80612ea4 = 1;
}

// -------------------------------------------------------------------------
// 0x80366970  grid_minigame_load_board - copy a numbered board template.
// -------------------------------------------------------------------------
void grid_minigame_load_board(int idx)
{
    copy_board(board_template(idx));
}

// -------------------------------------------------------------------------
// 0x803668a8  grid_minigame_init_random_board - pick+load an RNG board, build it.
//   Chooses one of three board templates by RNG, copies it into the live board,
//   resets the board scratch state, then builds the board entities and runs the
//   post-build init passes.
// -------------------------------------------------------------------------
void grid_minigame_init_random_board(unsigned long long a1, double a2, double a3, double a4,
                                     double a5, double a6, double a7, double a8)
{
    (void)a1;
    unsigned int r = ((RngTickFn)&rng_counter_tick)();
    int idx = (r % 3 != 0) ? (int)(r % 3 + 2) : 1;

    copy_board(board_template(idx));
    DAT_80612eac = 0;
    uRam80612eb0 = 0;
    DAT_80612ea8 = 0;
    DAT_80612ea4 = 1;

    grid_minigame_build_board(a1, a2, a3, a4, a5, a6, a7, a8);
    FUN_80367e08();
    FUN_80367fe4();
    DAT_80612ea0 = 0;
    FLOAT_80612eb4 = FLOAT_8061b27c;
}

// -------------------------------------------------------------------------
// 0x803669b8  grid_minigame_tick_fwd - tick trampoline.
// -------------------------------------------------------------------------
void grid_minigame_tick_fwd(unsigned long long a1, double a2, double a3, double a4,
                            double a5, double a6, double a7, double a8)
{
    (void)a1;
    unsigned long long inst = (unsigned long long)(unsigned int)FUN_8036815c();
    FUN_803685ac(inst, a2, a3, a4, a5, a6, a7, a8);
}

// -------------------------------------------------------------------------
// 0x803669dc  grid_minigame_reset_fwd - reset trampoline.
// -------------------------------------------------------------------------
void grid_minigame_reset_fwd(void)
{
    FUN_8036804c();
}

// -------------------------------------------------------------------------
// 0x803669fc  grid_minigame_render_lines - render the grid lines (GX).
//   Sets up the text/blend GX state, draws the interior grid lines cell by cell
//   over the rows x cols grid, then the border runs, and restores GX state.
//   x0/x1 and y0/y1 bound the board on screen.
// -------------------------------------------------------------------------
void grid_minigame_render_lines(unsigned long long a1, double y1, double x0, double x1)
{
    (void)a1;
    double y0 = (double)FUN_803826cc();
    float cellH = (float)((y1 - y0) / (double)(int)(DAT_805ec318 + 1));
    float cellW = (float)((x1 - x0) / (double)(int)(DAT_805ec31a + 1));

    unsigned int savedChans = gx_set_num_chans(0);
    int savedMat  = gx_set_sub_material_id(0xe);
    int savedBlend = gx_set_blend_mode(0);
    gx_set_color_update_mode(0);
    gx_set_sub_material_id(1);
    text_set_font_table(1);

    float thick = FLOAT_8061b284;
    for (int r = 0; r < (int)DAT_805ec318; r++) {
        double yy = (double)(float)((double)cellH * (double)(r + 1) + y0);
        for (int c = 0; c < (int)DAT_805ec31a; c++) {
            float minCell = (cellH < cellW) ? cellH : cellW;
            FUN_80368ae0((double)(float)(minCell / thick), yy,
                         (double)(float)((double)cellW * (double)(c + 1) + x0));
        }
    }

    gx_set_sub_material_id(1);
    int run = 0;
    for (int i = 0; i < DAT_805ec6f0; i++) {
        float minCell = (cellH < cellW) ? cellH : cellW;
        char a = (&DAT_805ec6f5)[run];
        char b = (&DAT_805ec6f4)[run];
        FUN_80368a78((double)(int)b, (double)(int)a);
        FUN_80368d7c((double)(float)((double)cellH * (double)((int)b + 1) + y0),
                     (double)(float)((double)cellW * (double)((int)a + 1) + x0),
                     (double)(float)(minCell / thick));
        char a2 = (&DAT_805ec6fd)[run];
        char b2 = (&DAT_805ec6fc)[run];
        FUN_80368a78((double)(int)b2, (double)(int)a2);
        FUN_80368d7c((double)(float)((double)cellH * (double)((int)b2 + 1) + y0),
                     (double)(float)((double)cellW * (double)((int)a2 + 1) + x0),
                     (double)(float)(minCell / thick));
        run += 2;
    }

    gx_set_sub_material_id(savedMat);
    gx_set_blend_mode(savedBlend);
    gx_set_num_chans(savedChans);
    FUN_80382718();
}

// -------------------------------------------------------------------------
// 0x80366dc8  grid_minigame_spawn_border_tile - spawn one 0x336 border tile.
//   Spawns a border tile entity at a computed grid position, tagging it with a
//   direction/kind derived from param_11, and writes its world position into
//   the entity. The exact rotation math is condensed (see file NOTE).
// -------------------------------------------------------------------------
void grid_minigame_spawn_border_tile(unsigned long long a1, double a2, double a3,
                                     unsigned long long a4, unsigned long long a5,
                                     unsigned long long a6, unsigned long long a7,
                                     unsigned long long a8, char* coord, float* origin,
                                     int kind)
{
    (void)a1;
    float spawnPos[3];
    spawnPos[0] = DAT_803e0c38;
    spawnPos[1] = (float)DAT_803e0c3c;
    spawnPos[2] = DAT_803e0c40;

    unsigned long long pair = FUN_803826f0();
    char* cell = (char*)(unsigned int)(pair >> 0x20);
    (void)cell; (void)coord;

    double sinA = math_poly_trig((double)FLOAT_8061b290);
    double cosA = math_poly_trig((double)FLOAT_8061b294);

    // Orientation from the tile's grid parity (condensed from the original's
    // modulo-180 test on the accumulated angle).
    float angle = (float)((double)FLOAT_8061b298 * (double)((int)coord[0]) - (double)FLOAT_8061b298);
    if (((int)angle % 0xb4) == 0) {
        spawnPos[2] = (angle == FLOAT_8061b2a8) ? FLOAT_8061b2a0 : FLOAT_8061b2a4;
    } else {
        spawnPos[1] = angle + FLOAT_8061b29c;
        spawnPos[0] = FLOAT_8061b2a0;
    }

    int ent = ((SpawnActFn)&entity_spawn_with_activation)(
        (double)spawnPos[1], (double)FLOAT_8061b298, a3, a4, a5, a6, a7, a8,
        1, 0x336, origin, spawnPos, 0, 0);

    if (kind == 0) {
        *(int*)(*(int*)(ent + 4) + 0x30) = 2;
    } else if (kind == 1) {
        *(int*)(*(int*)(ent + 4) + 0x30) = 1;
    } else {
        *(int*)(*(int*)(ent + 4) + 0x30) = 1 << kind;
    }

    // World position for the border tile (condensed coordinate mapping).
    float gx = (float)((double)(int)DAT_805ec318 - DOUBLE_8061b2b8) - FLOAT_8061b2ac;
    float gy = (float)((double)(int)DAT_805ec31a - DOUBLE_8061b2b8) - FLOAT_8061b2ac;
    *(float*)(ent + 0x64) = (float)(sinA * (double)(FLOAT_8061b2b0 * (float)(a2 * (double)gx)) + (double)origin[0]);
    *(float*)(ent + 0x68) = (float)(cosA * (double)(FLOAT_8061b2b0 * (float)(a2 * (double)gy)) + (double)origin[1]);
    *(float*)(ent + 0x6c) = FLOAT_8061b2b0 + origin[2];

    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x8036723c  grid_minigame_build_board - spawn all cell + border tiles.
//   Iterates the rows x cols grid; for each cell it spawns the tile entity for
//   that cell value (0x339 for 1, 0x337 for 0, 0x338 for 2, skipped for <0 or
//   >2), stores the entity back into the cell record (+0x14), positions and
//   colours it, and registers its update callbacks. After the grid it spawns
//   the border tiles and installs the border rotation matrix. The per-cell
//   coordinate math is condensed (see file NOTE).
// -------------------------------------------------------------------------
void grid_minigame_build_board(unsigned long long a1, double a2, double a3, double a4,
                               double a5, double a6, double a7, double a8)
{
    (void)a1;
    float* origin = (float*)FUN_803826c0();
    float border[3];
    border[0] = DAT_803e0c44;
    border[1] = (float)DAT_803e0c48;
    border[2] = DAT_803e0c4c;

    double sinA = math_poly_trig((double)FLOAT_8061b290);
    double cosA = math_poly_trig((double)FLOAT_8061b294);
    float mtl = FLOAT_8061b27c;
    float posScale = FLOAT_8061b2b0;
    float cellStep = FLOAT_8061b2ac;
    float colorVal = FLOAT_8061b2a4;

    int rowOff = 0;
    for (unsigned int r = 0; r < DAT_805ec318; r++, rowOff += 0x14) {
        int colOff = 0;
        for (unsigned int c = 0; c < DAT_805ec31a; c++, colOff += 0x8c) {
            int cellVal = *(int*)((int)&DAT_805ec318 + colOff + rowOff + 8);
            unsigned int type;
            if (cellVal == 1)      type = 0x339;
            else if (cellVal == 0) type = 0x337;
            else if (cellVal == 2) type = 0x338;
            else continue;   // <0 or >2: no tile

            int ent = ((SpawnActFn)&entity_spawn_with_activation)(
                cosA, a2, a3, a4, a5, a6, a7, a8, 1, type, origin, border, 0, 0);

            int* cellRec = (int*)((int)&DAT_805ec318 + colOff + rowOff + 0x14);
            *cellRec = ent;

            // Condensed world position for the cell (row/col mapped to X/Y).
            float px = (float)((double)(int)DAT_805ec318 - DOUBLE_8061b2b8) * cellStep;
            float py = (float)((double)(int)DAT_805ec31a - DOUBLE_8061b2b8) * cellStep;
            float wx = (float)(sinA * (double)(posScale * (float)(a2 * (double)(px - (float)(int)c))) + (double)origin[0]);
            float wy = (float)(cosA * (double)(posScale * (float)(a2 * (double)(py - (float)(int)r))) + (double)origin[1]);
            *(float*)(ent + 0x64) = wx;
            *(float*)(ent + 0x68) = wy;
            *(float*)(ent + 0x6c) = -(float)(posScale * (float)(a2 * (double)(py - (float)(int)r)) - border[2]);
            *(unsigned int*)(ent + 0x60) = (r << 8) | 0x50500000 | c;

            render_instance_set_material(mtl, *(int**)(ent + 4), 1);
            entity_register_for_update(ent, 3, (int)0x803675d0);
            entity_register_for_update(ent, 2, (int)0x80367a54);
            *(unsigned char*)(ent + 0x32) = 2;
            *(float*)(ent + 0xc8) = colorVal;
            *(unsigned int*)(ent + 0x28) |= 4;
            *(float*)(ent + 0x25c) = posScale;
            *(float*)(ent + 0x260) = mtl;
            entity_register_for_update(ent, 0, (int)0x8021e160);
        }
    }

    int run = 0;
    for (int i = 0; i < DAT_805ec6f0; i++, run += 2) {
        grid_minigame_spawn_border_tile(a1, a2, a3, a4, a5, a6, a7, a8,
                                        &DAT_805ec6f4 + run, origin, i);
        grid_minigame_spawn_border_tile(a1, a2, a3, a4, a5, a6, a7, a8,
                                        &DAT_805ec6fc + run, origin, i);
    }

    mtx4_set_rotation_z((double)FLOAT_8061b290, &DAT_805ec704);
    FUN_8038270c();
}

// ============================================================================
// Address range: 0x803675d0 - 0x80368d7b | batch_1031 | 2026-09-12
//
// Ekun grid minigame board - runtime continuation of batch_1030. This half is
// the live board's per-frame behaviour: the tile entity update+render callbacks
// registered by grid_minigame_build_board, the particle/texture setup run after
// a board is built, the per-frame cursor/edit tick behind grid_minigame_tick_fwd,
// the reset-time glow-texture bake behind grid_minigame_reset_fwd, and the
// path-solve pass that walks each border run through the grid to light the wall
// bit-flags and detect the end-of-round condition.
//
//   grid_minigame_tile_update    0x803675d0  entity update cb: wall bits + line-fade
//   grid_minigame_tile_render    0x80367a54  entity render cb: matrices + particles
//   grid_minigame_init_particles 0x80367e08  build the two spark particle emitters
//   grid_minigame_alloc_textures 0x80367fe4  allocate the 2 glow scratch RTs
//   grid_minigame_bake_glow      0x8036804c  render the 2 glow textures (GX)
//   grid_minigame_frame_update   0x8036815c  per-frame cursor nav + edit + sfx
//   grid_minigame_solve_paths    0x803685ac  trace border runs, set wall bits, end test
//   grid_minigame_trace_step     0x80368974  one step of the path tracer
//   grid_minigame_classify_dir   0x80368a78  classify a coordinate into a dir bucket
//   grid_minigame_draw_cursor    0x80368ae0  draw the cursor sprite strip + marks
//
// NOTE: non-matching decomp (see the batch_1030 NOTE above). The heavy paired-
// single float / CONCAT44 int-to-float coordinate arithmetic is condensed to
// equivalent float expressions; control flow, entity/tile types, GX/particle/
// sound side effects and the wall-bit bookkeeping match the original. The two
// grid callbacks read incoming register values that the decompiler surfaced as
// uninitialised locals - here they are taken as explicit typed parameters. The
// path-solve loop's grid state block layout (rows/cols, cell array at +0x14
// stride 0x8c, wall-bit word at +8) is reproduced from the original offsets.

// ---- per-file externs for raw symbols referenced by this half --------------
// (Engine helpers with canonical prototypes - collision_*, mtx4_*, particle_*,
//  gx_*, sprite_*, sound_*, math_poly_trig, ekun_nuke_reset - resolve through
//  generated_decls.h. Only raw data symbols and undeclared FUN_ helpers are
//  declared here. Where a call site's raw ABI differs from the canonical
//  prototype the call is issued through a typed function-pointer cast.)
extern int  DAT_806112d8;                    // active glow material id
extern int  DAT_805ec744, DAT_805ec748;      // glow scratch RT handles
extern int  DAT_805ec74c, DAT_805ec750;      // (paired render-target words)
extern int  DAT_80612610;                    // active player count
extern int  DAT_80611d78;                    // player record table base (stride 0x141c)
extern int  DAT_80612614;                    // grid sub-mode (0xb draw / 0x... erase)
extern int  iRam80612eb0;                    // cursor column live (alias of uRam80612eb0)

extern unsigned int FUN_8037afd0(void);           // player-is-grid-controller predicate
extern void FUN_8037b010(float*, float*, unsigned int*);  // read pad->grid cursor pos
extern void FUN_8037b05c(double, double, double, double, double, double, double, double); // non-nuke end
extern double FUN_80390ae4(double, double);        // fmod-style angle wrap
extern float DAT_80583228;                         // sin/cos lookup table (0x800 entries)
extern int  DAT_80610a20;                          // material table base ptr
extern int  DAT_80494470, DAT_80494480;            // border spark offset/count tables
extern short DAT_805ec320;                         // per-cell spark table index (stride 5/0x23)
extern int  DAT_805ec328;                          // solve scratch: per-cell visited (stride 0x8c/0x14)
extern char DAT_805ec324;                          // solve scratch: per-cell dir map
extern float DAT_805ec31c;                         // board block: first cell word (wall float)
// NOTE: DAT_805ec318/DAT_805ec31a declared as unsigned int in the batch_1030
// header block above; reuse those (do not re-declare here).
extern float FLOAT_806108ec;                       // frame-time numerator
extern int   DAT_806108e4;                         // start-frame counter (int form here)

// scratch-scope enter/leave variants used only by this half
extern int  FUN_803826c8(void);                    // scratch enter (solve: timer)
extern int  FUN_803826e4(void);                    // scratch enter (render: self entity)
extern void FUN_80382714(void);                    // scratch leave (solve)
extern void FUN_80382730(void);                    // scratch leave (render)

extern float DAT_803e0c50, DAT_803e0c58, DAT_803e0c68, DAT_803e0c70;
extern int   DAT_803e0c54, DAT_803e0c5c, DAT_803e0c60, DAT_803e0c64, DAT_803e0c6c;
extern int   DAT_805ec754;   // spark emitter def A (raw symbol; &-taken as ptr arg)
extern int   DAT_805ec7d8;   // spark emitter def B
extern int   DAT_805ec85c;   // particle def A name
extern int   DAT_805ec928;   // particle def B name

// call-site ABIs that differ from the canonical generated prototypes.
typedef void (*SpawnFn)(unsigned int, unsigned int, unsigned int, float*, float*);
typedef void (*ParticleDefFn)(double, double, void*, int, int, int, int, int);
typedef void (*EmitCreateFn)(double, double, double, void*, void*, int, int);
typedef void (*EmitScaleFn)(void*);
typedef void (*Mtx4TransFn)(double, double, double);
typedef void (*SpriteStrip4Fn)(double, double, double, double, double, double, double, double,
                               int, int, unsigned int, int, int, unsigned int, unsigned int,
                               unsigned int, float, unsigned int, unsigned int, unsigned int,
                               unsigned int);

// extra rodata floats used only by this half
extern float FLOAT_8061b2c0, FLOAT_8061b2c4, FLOAT_8061b2c8, FLOAT_8061b2cc, FLOAT_8061b2d0;
extern float FLOAT_8061b2d4, FLOAT_8061b2d8, FLOAT_8061b2dc, FLOAT_8061b2e0, FLOAT_8061b2e4, FLOAT_8061b2e8;
extern float FLOAT_8061b2ec, FLOAT_8061b2f0, FLOAT_8061b2f4, FLOAT_8061b2f8, FLOAT_8061b2fc;
extern float FLOAT_8061b300, FLOAT_8061b304, FLOAT_8061b308, FLOAT_8061b30c, FLOAT_8061b310, FLOAT_8061b314;
extern float FLOAT_8061b318, FLOAT_8061b328, FLOAT_8061b32c;
extern double DOUBLE_8061b320;
// DAT_80612eac / uRam80612eb0 / DAT_80612ea0 declared in the batch_1030 block above.

// -------------------------------------------------------------------------
// 0x803675d0  grid_minigame_tile_update - tile entity update callback.
//   Registered as update id 3 by grid_minigame_build_board. Rebuilds the tile's
//   collision strip/face tables, advances the wall "unfold" angle toward its
//   target (wrapping into range), tweens the tile alpha over the round timer,
//   and rebuilds the per-tile wall face indices from the cell's wall-bit word.
//   `inst` is the tile entity (r3 on entry).
// -------------------------------------------------------------------------
void FUN_803675d0(int inst)
{
    int stripTab = collision_alloc_strip_table_extended(*(int**)(inst + 4), 4);
    int faceTab  = collision_alloc_face_table(*(int**)(inst + 4));
    bool atRest = false;

    DAT_806112d8 = *(int*)(stripTab + 0x10);
    *(int*)(stripTab + 0x20) = DAT_805ec744;
    *(int*)(stripTab + 0x30) = DAT_805ec748;
    *(int*)(stripTab + 0x40) = DAT_805ec74c;
    *(int*)(stripTab + 0x50) = DAT_805ec750;
    *(int*)(stripTab + 0x60) = 0xffffffff;

    unsigned int col = *(unsigned int*)(inst + 0x60) & 0xff;
    unsigned int row = *(unsigned int*)(inst + 0x60) >> 8 & 0xff;
    float* cellAngle = (float*)(&DAT_805ec318 + col * 0x46 + row * 10 + 2);

    float target = FLOAT_8061b2c0 * -(*cellAngle);
    if (*(int*)(&DAT_805ec318 + col * 0x46 + row * 10 + 4) == 1) {
        target = -target;
    }

    if (*(float*)(inst + 0x1a4) <= FLOAT_8061b2c4) {
        atRest = true;
    } else {
        float cur = *(float*)(inst + 0xd0);
        float delta = target - cur;
        float t = delta;
        while (t <= FLOAT_8061b2cc) t += FLOAT_8061b2c8;
        while (FLOAT_8061b2c0 < t) t -= FLOAT_8061b2c8;
        if (t <= FLOAT_8061b2d0) {
            while (delta <= FLOAT_8061b2cc) delta += FLOAT_8061b2c8;
            while (FLOAT_8061b2c0 < delta) delta -= FLOAT_8061b2c8;
            if (FLOAT_8061b2d4 <= delta) {
                atRest = true;
            } else {
                target = cur - FLOAT_8061b2d0;
            }
        } else {
            target = FLOAT_8061b2d0 + cur;
        }
        *(float*)(inst + 0xd0) = target;
    }

    // Alpha tween over the round timer (up when this is the cursor cell).
    if (((row == (unsigned int)DAT_80612eac) && (col == (unsigned int)uRam80612eb0) &&
         (DAT_80612ea0 != 0)) &&
        (*(int*)(DAT_80612ea0 + 0x114) == 0x100) && (*(int*)(DAT_80612ea0 + 0x1390) == 0x1c)) {
        double startF = (double)(int)DAT_806108e4;
        *(float*)(inst + 0x1a4) =
            (FLOAT_8061b2d8 / (float)((double)(int)*(int*)&DAT_806109c0 - DOUBLE_8061b288)) *
            (float)(-startF - DOUBLE_8061b288) + *(float*)(inst + 0x1a4);
    } else if (atRest) {
        double denom = (double)(int)*(int*)&DAT_806109c0;
        *(float*)(inst + 0x1a4) =
            -((FLOAT_8061b2d8 / (float)(-denom - DOUBLE_8061b288)) *
              (float)((double)(-(int)DAT_806108e4) - DOUBLE_8061b288) - *(float*)(inst + 0x1a4));
    }
    if (FLOAT_8061b280 < *(float*)(inst + 0x1a4)) *(float*)(inst + 0x1a4) = FLOAT_8061b280;
    if (*(float*)(inst + 0x1a4) < FLOAT_8061b27c) *(float*)(inst + 0x1a4) = FLOAT_8061b27c;

    // Wall face indices from the tile type + wall-bit word.
    unsigned short tileType = *(unsigned short*)(inst + 0x94);
    int matId, faceBase;
    if (tileType == 0x338)      { matId = 4; faceBase = 2; }
    else if (tileType == 0x337) { matId = 4; faceBase = 1; }   // (0x336..0x337)
    else if (tileType == 0x339) { matId = 3; faceBase = 1; }   // (0x338..0x339)
    else                        { matId = 0; faceBase = 0; }

    unsigned int walls = *(unsigned int*)(&DAT_805ec318 + col * 0x46 + row * 10 + 8);
    int* faceSlot = (int*)(faceTab + faceBase * 0x30);
    for (int i = 0; i < 4; i++) {
        if (walls & 1) FUN_80369004(0, inst, i, faceTab, matId, i);
        if (walls & 4) FUN_80369004(2, inst, i, faceTab, matId, i);
        if (walls & 8) FUN_80369004(3, inst, i, faceTab, matId, i);
        if (walls & 2) FUN_80369004(1, inst, i, faceTab, matId, i);
        if (walls & 0xf) {
            if (*(short*)(inst + 0x94) == 0x337) {
                float av = *cellAngle;
                bool aWhole = (av == (float)((double)(-(int)av) - DOUBLE_8061b288));
                if (((walls & 5) == 0 || !aWhole) && ((walls & 10) == 0 || aWhole)) {
                    *faceSlot = i + 2;
                } else {
                    faceSlot[0xc] = i + 2;
                }
            } else {
                *faceSlot = i + 2;
            }
        }
        walls >>= 4;
    }
}

// -------------------------------------------------------------------------
// 0x80367a54  grid_minigame_tile_render - tile entity render callback.
//   Registered as render id 2 by grid_minigame_build_board. Resets the tile's
//   bone matrices, builds the "unfold" rotation from the tile's timer, then
//   either spawns the trailing spark particles for the active cursor cell or a
//   one-shot spark when the alpha crosses the fade threshold, and flags the
//   tile collision dirty. `inst` is the tile entity (r3 on entry).
// -------------------------------------------------------------------------
void FUN_80367a54(int inst)
{
    (void)inst;
    int self = FUN_803826e4();
    float trans = FLOAT_8061b27c;
    int mtxOff = 0;
    int rig = *(int*)(self + 4);
    for (int i = 0; i < *(int*)(*(int*)(rig + 4) + 4); i++) {
        mtx4_set_identity(*(int*)(rig + 0x10) + mtxOff);
        mtxOff += 0x40;
    }

    float alpha = *(float*)(self + 0x1a4);
    unsigned int row = *(unsigned int*)(self + 0x60) >> 8 & 0xff;
    unsigned int col = *(unsigned int*)(self + 0x60) & 0xff;
    double angleY = (double)*(float*)(self + 0xd0);
    float scale;
    if (alpha <= FLOAT_8061b2c4) {
        scale = FLOAT_8061b2e4 * (alpha / FLOAT_8061b2c4);
    } else {
        float k;
        if ((row == (unsigned int)DAT_80612eac) && (col == (unsigned int)uRam80612eb0)) {
            k = (FLOAT_8061b280 - alpha) / FLOAT_8061b2e0;
            k = FLOAT_8061b2d8 - k * k;
        } else {
            k = (alpha - FLOAT_8061b2c4) / FLOAT_8061b2e0;
            k = k * k;
        }
        trans = FLOAT_8061b2dc * k;
        scale = FLOAT_8061b2e4;
    }

    rig = *(int*)(self + 4);
    mtx4_set_identity(*(int*)(rig + 0x10));
    mtx4_set_identity(*(int*)(*(int*)(self + 4) + 0x10) + 0x180);
    *(unsigned int*)(self + 0x28) |= 0x200000;
    mtx4_set_rotation_y(angleY, (float*)(*(int*)(*(int*)(self + 4) + 0x10) + 0x140));
    mtx4_write_translation((double)FLOAT_8061b27c, (double)trans, (double)FLOAT_8061b27c,
                           *(int*)(*(int*)(self + 4) + 0x10) + 0x140);
    // Four corner translations (raw 3-arg float ABI at 0x8038xxxx).
    ((Mtx4TransFn)&mtx4_set_translation)((double)scale, (double)FLOAT_8061b27c, (double)scale);
    ((Mtx4TransFn)&mtx4_set_translation)((double)scale, (double)FLOAT_8061b27c, -(double)scale);
    ((Mtx4TransFn)&mtx4_set_translation)(-(double)scale, (double)FLOAT_8061b27c, -(double)scale);
    ((Mtx4TransFn)&mtx4_set_translation)(-(double)scale, (double)FLOAT_8061b27c, (double)scale);

    float cur = *(float*)(self + 0x1a4);
    double denom = (double)(int)*(int*)&DAT_806109c0;
    float prev = -((FLOAT_8061b2d8 / (float)(-denom - DOUBLE_8061b288)) * FLOAT_806108ec - cur);

    if ((prev <= FLOAT_8061b2c4) || (FLOAT_8061b2c4 < cur)) {
        if ((prev <= FLOAT_8061b2c4) && (FLOAT_8061b2c4 < cur)) {
            float sparkB[3];
            *(int*)&sparkB[0] = DAT_803e0c5c;
            *(int*)&sparkB[1] = DAT_803e0c60;
            *(int*)&sparkB[2] = DAT_803e0c64;
            ((SpawnFn)&particle_emitter_spawn)(0, (unsigned int)&DAT_805ec7d8, 0, (float*)(self + 100), sparkB);
        }
    } else {
        float sparkA[3];
        *(int*)&sparkA[0] = *(int*)&DAT_803e0c50;
        *(int*)&sparkA[1] = DAT_803e0c54;
        *(int*)&sparkA[2] = *(int*)&DAT_803e0c58;
        int off = 0;
        int sparkIdx = (&DAT_805ec320)[row * 5 + col * 0x23];
        for (int n = 0; n < *(int*)(&DAT_80494480 + sparkIdx * 0x14); n++) {
            int* rigPtr = *(int**)(*(int*)(self + 4) + 4);
            float* base = (float*)(rigPtr[*rigPtr * -0x30 + 0x1b] +
                          *(int*)(&DAT_80494470 + off + sparkIdx * 0x14) * 0x18);
            float pos[3];
            pos[0] = base[0]; pos[1] = base[1]; pos[2] = base[2];
            mtx4_transform_vector((float*)(*(int*)(*(int*)(self + 4) + 0x10) + 0x140), pos);
            pos[0] += *(float*)(self + 100);
            pos[1] += *(float*)(self + 0x68);
            pos[2] += *(float*)(self + 0x6c);
            ((SpawnFn)&particle_emitter_spawn)(0, (unsigned int)&DAT_805ec754, 0, pos, sparkA);
            off += 4;
        }
    }

    collision_set_flags(*(int**)(self + 4), 2);
    *(unsigned int*)(self + 0x28) |= 0x200000;
    FUN_80382730();
}

// -------------------------------------------------------------------------
// 0x80367e08  grid_minigame_init_particles - build the two spark emitters.
//   Defines and creates the "trail" spark (DAT_805ec754/85c) and the "burst"
//   spark (DAT_805ec7d8/928) particle emitters used by the tile render callback,
//   then applies their colour ranges and a shared scale.
// -------------------------------------------------------------------------
void FUN_80367e08(void)
{
    float scale[3];
    *(int*)&scale[0] = DAT_803e0c68;
    *(int*)&scale[1] = DAT_803e0c6c;
    *(int*)&scale[2] = DAT_803e0c70;

    // The two def-builders take an 8-arg raw ABI (two f-regs packed into the
    // leading double, then the def-name ptr and five int flags); issue through
    // a typed cast to the raw entry, matching the original register layout.
    ((ParticleDefFn)&particle_def_create)((double)FLOAT_8061b2ac, (double)FLOAT_8061b2d8, &DAT_805ec85c, 0, 0, 1, 0x28c, 0);
    particle_def_set_size_keyframes(FLOAT_8061b2d8, FLOAT_8061b27c, FLOAT_8061b27c, FLOAT_8061b2e8,
                                    0x7f7f7f7f, 0x7060307f, 0);
    particle_def_set_size_start_end(FLOAT_8061b29c, FLOAT_8061b2ec, FLOAT_8061b2f0, FLOAT_8061b2f0);
    particle_def_set_color_end(FLOAT_8061b2f4, FLOAT_8061b2f8, FLOAT_8061b2d8, FLOAT_8061b27c);
    particle_def_set_velocity_ranges(FLOAT_8061b2f4, FLOAT_8061b2fc, FLOAT_8061b2fc, FLOAT_8061b2fc,
                                     FLOAT_8061b2d8, FLOAT_8061b2c4);
    ((EmitCreateFn)&particle_emitter_create)((double)FLOAT_8061b300, (double)FLOAT_8061b27c, (double)FLOAT_8061b2ac,
                            &DAT_805ec754, &DAT_805ec85c, 0, 1);
    particle_emitter_set_color_range(FLOAT_8061b27c, FLOAT_8061b27c, FLOAT_8061b304, FLOAT_8061b27c);

    ((ParticleDefFn)&particle_def_create)((double)FLOAT_8061b2d8, (double)FLOAT_8061b2d8, &DAT_805ec928, 0, 1, 1, 0x326, 0);
    particle_def_set_size_keyframes(FLOAT_8061b2d8, FLOAT_8061b27c, FLOAT_8061b27c, FLOAT_8061b2e8,
                                    0x7f7f7f7f, 0x7070707f, 0);
    particle_def_set_size_start_end(FLOAT_8061b29c, FLOAT_8061b2ec, FLOAT_8061b2f0, FLOAT_8061b2f0);
    particle_def_set_color_end(FLOAT_8061b2f4, FLOAT_8061b2f8, FLOAT_8061b308, FLOAT_8061b27c);
    particle_def_set_velocity_ranges(FLOAT_8061b2e4, FLOAT_8061b2fc, FLOAT_8061b2c4, FLOAT_8061b2e4,
                                     FLOAT_8061b2d8, FLOAT_8061b2c4);
    ((EmitCreateFn)&particle_emitter_create)((double)FLOAT_8061b30c, (double)FLOAT_8061b27c, (double)FLOAT_8061b2ac,
                            &DAT_805ec7d8, &DAT_805ec928, 1, 1);
    particle_emitter_set_color_range(FLOAT_8061b27c, FLOAT_8061b27c, FLOAT_8061b310, FLOAT_8061b27c);
    ((EmitScaleFn)&particle_emitter_set_scale)(scale);
}

// -------------------------------------------------------------------------
// 0x80367fe4  grid_minigame_alloc_textures - allocate the 2 glow scratch RTs.
// -------------------------------------------------------------------------
void FUN_80367fe4(void)
{
    FUN_803826f0();
    DAT_806112d8 = sprite_get_texture_id(0);
    int off = 0;
    for (int i = 0; i < 2; i++) {
        render_target_alloc_scratch(0x10, 0x10, (int*)((int)&DAT_805ec744 + off));
        off += 4;
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x8036804c  grid_minigame_bake_glow - render the 2 glow textures (GX).
//   Called from grid_minigame_reset_fwd. Draws the base glow sprite into each
//   of the two scratch render targets with an increasing red-channel fill, then
//   restores the GX material/blend state.
// -------------------------------------------------------------------------
void FUN_8036804c(void)
{
    int savedMat = gx_set_sub_material_id(1);
    int savedBlend = gx_set_blend_mode(0);
    int shift = 0;
    int rtOff = 0;
    for (int i = 0; i < 2; i++) {
        gx_set_color_update_mode(0);
        gx_set_active_material(DAT_806112d8);
        gx_draw_sprite_rect(0, 0, 0x10, 0x10, 0, 0, 0x100, 0x100);
        gx_set_color_update_mode(0);
        gx_fill_rect_clipped(0, 0, 0x10, 0x10, 0x7f << (shift + 8) | 8);
        render_target_bind_default(*(int*)((int)&DAT_805ec744 + rtOff));
        shift += 8;
        rtOff += 4;
    }
    gx_fill_rect_flags(0xf, 0, 0, 0x10, 0x10, 0);
    gx_set_sub_material_id(savedMat);
    gx_set_blend_mode(savedBlend);
}

// -------------------------------------------------------------------------
// 0x8036815c  grid_minigame_frame_update - per-frame cursor nav + edit + sfx.
//   Called from grid_minigame_tick_fwd. Finds the active grid controller
//   player, then either moves the integer cursor by the analog stick (arcade
//   controller mode) or maps the pad's absolute cursor position onto the grid
//   (edit/other mode), clamps into range, plays the move/edit sounds, and in
//   the edit sub-mode nudges the target cell's angle and toggles its wall bit.
//   Returns nothing meaningful; the trampoline ignores the value.
// -------------------------------------------------------------------------
int FUN_8036815c(void)
{
    FUN_803826f0();
    int rec = 0;
    for (int p = 0; p < DAT_80612610; p++) {
        int slot = *(int*)(DAT_80611d78 + rec + 0x114);
        if (((slot == 0x100) && (*(int*)(DAT_80611d78 + rec + 0x1390) == 0x1c)) ||
            (slot == 0x2000) || (FUN_8037afd0() != 0)) {
            DAT_80612ea0 = DAT_80611d78 + rec;
        }
        rec += 0x141c;
    }

    if ((DAT_80612ea0 != 0) && (DAT_80612eac != -1)) {
        int prevCol = DAT_80612eac;
        int prevRow = iRam80612eb0;
        unsigned int flags = 0;
        int mode = *(int*)(DAT_80612ea0 + 0x114);

        if (mode == 0x100) {
            flags = grid_minigame_read_input(*(int*)DAT_80612ea0);
            if (flags & 2)      DAT_80612eac++;
            else if (flags & 1) DAT_80612eac--;
            if (flags & 8)      iRam80612eb0++;
            else if (flags & 4) iRam80612eb0--;
            if (DAT_80612eac < 0) DAT_80612eac = 0;
            if (iRam80612eb0 < 0) iRam80612eb0 = 0;
            if ((int)(unsigned int)DAT_805ec318 <= DAT_80612eac) DAT_80612eac = DAT_805ec318 - 1;
            if ((int)(unsigned int)DAT_805ec31a <= iRam80612eb0) iRam80612eb0 = DAT_805ec31a - 1;
            flags &= 0x80;
        } else if ((mode == 0x2000) || (FUN_8037afd0() != 0)) {
            float cx, cy;
            FUN_8037b010(&cx, &cy, &flags);
            float mapX = cx * (float)((double)(int)(DAT_805ec318 + 1) - DOUBLE_8061b288) - FLOAT_8061b2ac;
            float mapY = cy * (float)((double)(int)(DAT_805ec31a + 1) - DOUBLE_8061b288) - FLOAT_8061b2ac;
            float maxX = (float)((double)(int)(unsigned int)DAT_805ec318 - DOUBLE_8061b2b8);
            if (mapX <= maxX) {
                if (mapX < FLOAT_8061b27c) { mapX = FLOAT_8061b27c; flags = 0; }
            } else { mapX = maxX; flags = 0; }
            float maxY = (float)((double)(int)(unsigned int)DAT_805ec31a - DOUBLE_8061b2b8);
            if (mapY <= maxY) {
                if (mapY < FLOAT_8061b27c) { mapY = FLOAT_8061b27c; flags = 0; }
            } else { mapY = maxY; flags = 0; }
            DAT_80612eac = (int)mapX;
            iRam80612eb0 = (int)mapY;
        }

        if ((((mode == 0x100) && (*(int*)(DAT_80612ea0 + 0x1390) == 0x1c)) ||
             (mode == 0x2000)) || (FUN_8037afd0() != 0)) {
            if ((DAT_80612eac != prevCol) || (iRam80612eb0 != prevRow)) {
                int sfx;
                sound_init_play_params((int)&sfx);
                sfx = 0x3ac;
                sound_play_request(&sfx);
            }
            if (flags != 0) {
                float* cellAngle = (float*)(&DAT_805ec318 + iRam80612eb0 * 0x46 + DAT_80612eac * 10 + 2);
                if (DAT_80612614 == 0xb) *cellAngle += FLOAT_8061b2ac;
                else                     *cellAngle -= FLOAT_8061b2ac;
                float* wallTog = (float*)(&DAT_805ec31c + DAT_80612eac * 5 + iRam80612eb0 * 0x23);
                if (*wallTog < FLOAT_8061b27c)  *wallTog += FLOAT_8061b2b0;
                if (FLOAT_8061b2b0 <= *wallTog) *wallTog -= FLOAT_8061b2b0;
                int sfx2;
                sound_init_play_params((int)&sfx2);
                sfx2 = 0x3ad;
                sound_play_request(&sfx2);
            }
        }
    }
    FUN_8038273c();
    return 0;
}

// -------------------------------------------------------------------------
// 0x803685ac  grid_minigame_solve_paths - trace border runs, set wall bits.
//   Clears the per-cell visited scratch, then for each border run walks a path
//   through the grid one cell at a time (grid_minigame_trace_step), OR-ing the
//   traversed wall bits into each cell's wall-bit word. When every run reaches
//   its paired endpoint the board is solved: it either fires the ekun "nuke"
//   reset (grid sub-mode 0xb, on timeout / lost controller) or the normal
//   end-of-round handler.
// -------------------------------------------------------------------------
void FUN_803685ac(unsigned long long inst, double a2, double a3, double a4,
                  double a5, double a6, double a7, double a8)
{
    (void)inst;
    double timer = (double)FUN_803826c8();
    int solved = 0;
    int cellRow = 0;
    for (int r = 0; r < (int)(unsigned int)DAT_805ec318; r++) {
        int cellCol = 0;
        for (int c = 0; c < (int)(unsigned int)DAT_805ec31a; c++) {
            *(int*)((int)&DAT_805ec328 + cellRow + cellCol) = 0;
            cellCol += 0x8c;
        }
        cellRow += 0x14;
    }

    int runBit = 0;
    int runIdx = 0;
    int runOff = 0;
    while (true) {
        if (DAT_805ec6f0 <= runIdx) {
            if (solved == DAT_805ec6f0) {
                if (DAT_80612614 == 0xb) {
                    double denomStart = (double)(int)*(int*)&DAT_806109c0;
                    double numer = (double)(int)DAT_806108e4;
                    double rate = (double)(FLOAT_8061b2d8 / (float)(-denomStart - DOUBLE_8061b288));
                    double step = (double)(float)(-numer - DOUBLE_8061b288);
                    FLOAT_80612eb4 = (float)(step * rate + (double)FLOAT_80612eb4);
                    if (((double)FLOAT_8061b314 < (double)FLOAT_80612eb4) ||
                        ((DAT_80612ea0 != 0) && (*(int*)(DAT_80612ea0 + 0x114) != 0x100))) {
                        ekun_nuke_reset((double)FLOAT_80612eb4, rate, step, DOUBLE_8061b288,
                                        a5, a6, a7, a8);
                    }
                    DAT_80612eac = 0xffffffff;
                    uRam80612eb0 = 0xffffffff;
                } else {
                    FUN_8037b05c(timer, a2, a3, a4, a5, a6, a7, a8);
                }
            }
            FUN_80382714();
            return;
        }

        for (int endpoint = 0; endpoint < 2; endpoint++) {
            bool done = false;
            char sc, sr;
            if (endpoint == 0) { sc = (&DAT_805ec6f4)[runOff]; sr = (&DAT_805ec6f5)[runOff]; }
            else               { sc = (&DAT_805ec6fc)[runOff]; sr = (&DAT_805ec6fd)[runOff]; }

            int col = (int)sc, row = (int)sr;
            int dir;
            if (col < 0)                                    dir = 1;
            else if (row < 0)                               dir = 0;
            else if (col < (int)(unsigned int)DAT_805ec318) dir = 2;
            else                                            dir = 3;

            // Clamp start into range (branchless in the original).
            if (col > (int)(DAT_805ec318 - 1)) col = DAT_805ec318 - 1;
            if (col < 0) col = 0;
            if (row > (int)(DAT_805ec31a - 1)) row = DAT_805ec31a - 1;
            if (row < 0) row = 0;

            while (!done) {
                double wallVal = (double)(float)(&DAT_805ec31c)[col * 5 + row * 0x23];
                int coord2[2]; coord2[0] = col; coord2[1] = row;
                int next = FUN_80368974(wallVal, coord2, dir);
                if (next != -1) {
                    int back = dir - 2;
                    if (back < 0) back = dir + 2;
                    *(unsigned int*)(&DAT_805ec318 + row * 0x46 + col * 10 + 8) |=
                        (1 << back | 1 << next) << runBit;
                }
                if (next == 1)       col++;
                else if (next < 1) { if (next == -1) done = true; else if (-2 < next) row++; }
                else if (next == 3)  col--;
                else if (next < 3)   row--;
                dir = next;
                if ((col < 0) || ((int)(unsigned int)DAT_805ec318 <= col) ||
                    (row < 0) || ((int)(unsigned int)DAT_805ec31a <= row)) {
                    done = true;
                }
            }

            if ((endpoint == 0) && (col == (int)(char)(&DAT_805ec6fc)[runOff]) &&
                (row == (int)(char)(&DAT_805ec6fd)[runOff])) {
                solved++;
                break;
            }
        }
        runIdx++;
        runBit += 4;
        runOff += 2;
    }
}

// -------------------------------------------------------------------------
// 0x80368974  grid_minigame_trace_step - one step of the path tracer.
//   Given the current cell's wall value, the current (col,row) and incoming
//   direction, rotates the direction by the wall's turn amount, then reads the
//   cell's stored connection for that direction and rotates it the same way,
//   returning the outgoing direction (0..3) or -1 for a dead end.
// -------------------------------------------------------------------------
int FUN_80368974(double wallVal, int* coord, int dir)
{
    int turn = (int)((double)FLOAT_8061b2b0 * wallVal);
    if (turn == 2) dir -= 2;
    else if (turn < 2) { if ((0 < turn) && (--dir < 0)) dir = 3; }
    else if (turn < 4) dir += 1;
    if (dir < 0) dir += 4;
    if (3 < dir) dir -= 4;

    int out = (int)*(char*)((int)&DAT_805ec324 + coord[1] * 0x8c + coord[0] * 0x14 + dir);
    if (out != -1) {
        if (turn == 2) out -= 2;
        else if (turn < 2) { if ((0 < turn) && (++out < 0)) out = 3; }
        else if (turn < 4) out -= 1;
        if (out < 0) out += 4;
        if (3 < out) out -= 4;
    }
    return out;
}

// -------------------------------------------------------------------------
// 0x80368a78  grid_minigame_classify_dir - classify a coordinate into a bucket.
//   Returns 1 if col<0, 0 if row<0, 2 if col is inside the board, else 3.
//   Used by the line renderer to pick a border-edge direction.
// -------------------------------------------------------------------------
int FUN_80368a78(double col, double row)
{
    if ((double)FLOAT_8061b27c <= col) {
        if ((double)FLOAT_8061b27c <= row) {
            if (col < (double)(float)((double)(int)(unsigned int)DAT_805ec318 - DOUBLE_8061b2b8))
                return 2;
            return 3;
        }
        return 0;
    }
    return 1;
}

// -------------------------------------------------------------------------
// 0x80368ae0  grid_minigame_draw_cursor - draw the cursor sprite strip + marks.
//   Builds a 4-vertex sprite strip around the cursor from the sin/cos lookup
//   table (angle from the cursor's fractional position), picks the cursor icon
//   texture by state, submits it through the GX sprite-strip path, then draws
//   the per-side wall marks by walking the cursor cell's wall-bit nibble.
//   x/y are the cursor world origin; `scale` is the incoming f1 size.
// -------------------------------------------------------------------------
void FUN_80368ae0(double scale, double x, double y)
{
    unsigned long long packed = FUN_803826f0();
    float* cur = (float*)(unsigned int)(packed >> 0x20);
    unsigned int color = (unsigned int)packed;
    int tex = -1;

    double ang = FUN_80390ae4((double)((FLOAT_8061b2c0 * cur[0] + FLOAT_8061b2c0) - FLOAT_8061b318),
                              DOUBLE_8061b320);
    ang = (double)((float)ang - FLOAT_8061b2c0);
    float state = cur[1];
    if (state == 1.4013e-45f) {         // state == 1 (bit pattern of int 1 as float)
        tex = sprite_get_texture_id(0x3b1);
        ang = (double)(float)(-ang - (double)FLOAT_8061b328);
    } else if ((int)state < 1) {
        if (-1 < (int)state) tex = sprite_get_texture_id(0x3af);
    } else if ((int)state < 3) {
        tex = sprite_get_texture_id(0x3b0);
    }
    gx_set_active_material(tex);

    float verts[10];
    int vi = 0;
    for (int i = 4; i != 0; i--) {
        double sinTerm = (double)FLOAT_8061b32c * ang;
        ang = (double)(float)(ang + (double)FLOAT_8061b328);
        unsigned int idx = (unsigned int)(sinTerm + (double)FLOAT_8061b2ac);
        verts[vi]     = (float)(scale * (double)*(float*)((int)&DAT_80583228 + ((idx + 0x200) * 4 & 0x1ffc)) + x);
        verts[vi + 1] = (float)(scale * (double)(&DAT_80583228)[idx & 0x7ff] + y);
        if ((double)FLOAT_8061b2c0 < ang) ang = (double)(float)(ang - (double)FLOAT_8061b2c8);
        vi += 2;
    }

    int mat = *(int*)&DAT_80610a20 + tex * 0x58;
    unsigned int u = (unsigned int)*(unsigned short*)(mat + 0x4e) << 4;
    unsigned int v = (unsigned int)*(unsigned short*)(mat + 0x52) << 4;
    // Raw 21-arg strip ABI (differs from the 19-arg canonical prototype).
    ((SpriteStrip4Fn)&gx_draw_sprite_strip4)(
        (double)FLOAT_8061b2f4,
        (double)verts[0], (double)verts[1], (double)verts[2], (double)verts[3],
        (double)verts[6], (double)verts[7], (double)verts[8],
        0, 0, u, 0, 0, v, u, v, verts[5], color, color, color, color);

    unsigned int walls = (unsigned int)cur[3];
    for (int i = 0; i < 4; i++) {
        if (walls & 1) FUN_80368d7c(x, y, scale);
        if (walls & 4) FUN_80368d7c(x, y, scale);
        if (walls & 8) FUN_80368d7c(x, y, scale);
        if (walls & 2) FUN_80368d7c(x, y, scale);
        walls >>= 4;
    }
    FUN_8038273c();
}

// ============================================================================
// Address range: 0x80368d7c - 0x803694e3 | batch_1032 (grid tail) | 2026-09-12
//
// Final three functions of the ekun grid minigame board module (see the
// batch_1030/1031 NOTEs). These are the border-segment sprite draw used by the
// line renderer and the cursor, the wall-face + spark helper invoked per wall
// bit by grid_minigame_tile_update, and the per-cell glow lighting update
// registered as the tile entity's skinned-lighting callback.
//
//   grid_minigame_draw_border_seg 0x80368d7c  draw one border-run sprite segment
//   grid_minigame_wall_face_spark 0x80369004  set a wall face index + spawn a spark
//   grid_minigame_glow_lighting   0x803691b0  per-cell proximity glow lighting update
//
// NOTE: non-matching (condensed float coordinate math; see batch_1030 NOTE).
// ============================================================================

#undef FUN_80368d7c
#undef FUN_80369004

// per-file externs for raw symbols used only by this tail.
extern int  DAT_80611928;                    // global animated pulse counter
extern float FLOAT_8061b330, FLOAT_8061b338, FLOAT_8061b33c;
extern void spark_spawn_fading(void);
extern void spark_set_last_scale(double);
extern void spark_set_last_lifetime(double);
extern void spark_set_last_color(unsigned int);
extern int  FUN_803826e8(void);              // scratch enter (glow: instance ptr low/high)
extern void FUN_80382734(void);              // scratch leave (glow)

typedef void (*BorderStripFn)(double, double, double, double, double, double, double, double,
                              int, int, unsigned int, unsigned int, unsigned int, int, unsigned int,
                              unsigned int, float, unsigned int, unsigned int, unsigned int,
                              unsigned int);

// -------------------------------------------------------------------------
// 0x80368d7c  grid_minigame_draw_border_seg - draw one border-run sprite segment.
//   Reads the border direction (packed high word of the scratch enter) and the
//   animated pulse colour, orients a short sprite quad along that edge, and
//   submits it through the GX sprite-strip path with texture 0x33a. a2/a3 are
//   the edge midpoint / half-length in board space.
// -------------------------------------------------------------------------
void grid_minigame_draw_border_seg(double a1, double a2, double a3)
{
    (void)a1;
    long long packed = (long long)FUN_803826f0();
    int dir = (int)((unsigned long long)packed >> 0x20);
    float ext = FLOAT_8061b27c;
    bool flip = false;

    // Animated pulse intensity (triangle wave over the global pulse counter).
    unsigned int pulse = (DAT_80611928 % 0x7e < 0x3f)
        ? (0x7f - DAT_80611928 % 0x3f)
        : (DAT_80611928 % 0x3f + 0x40);
    pulse = (pulse & 0xff) << ((int)packed * 8 + 8);
    unsigned int color = (pulse | 0x7f) << 0x18 | (pulse & 0xff00) << 8 |
                         pulse >> 0x18 | pulse >> 8 & 0xff00;

    double along = ext;      // f1 (default from scratch)
    double perp1 = ext;
    double perp2 = (double)FLOAT_8061b27c;
    double base  = a2;
    if (dir == 2) {
        flip = true;
        ext = (double)(float)((double)FLOAT_8061b330 * a3);
        base = (double)(float)(a2 - a3);
    } else if (packed < 0x200000000LL) {
        if (dir == 0) {
            flip = true;
            ext = (double)(float)((double)FLOAT_8061b330 * a3);
            base = (double)(float)(a2 + a3);
        } else if (0 <= packed) {
            perp1 = (double)(float)(perp1 + a3);
            along = (double)(float)((double)FLOAT_8061b330 * a3);
        }
    } else if (packed < 0x400000000LL) {
        perp1 = (double)(float)(perp1 - a3);
        along = (double)(float)((double)FLOAT_8061b330 * a3);
    }

    int tex = sprite_get_texture_id(0x33a);
    gx_set_active_material(tex);
    tex = sprite_get_texture_id(0x33a);
    unsigned int u = (unsigned int)*(unsigned short*)(*(int*)&DAT_80610a20 + tex * 0x58 + 0x4e) << 4;
    tex = sprite_get_texture_id(0x33a);
    unsigned int v = (unsigned int)*(unsigned short*)(*(int*)&DAT_80610a20 + tex * 0x58 + 0x52) << 4;

    if (flip) {
        ((BorderStripFn)&gx_draw_sprite_strip4)(
            (double)FLOAT_8061b2f4,
            (double)(float)(perp2 - ext), (double)(float)(a2 - along),
            (double)(float)(perp2 + ext), (double)(float)(a2 - along),
            (double)(float)(perp1 - ext), (double)(float)(base + along),
            (double)(float)(perp1 + ext),
            0, 0, 0, v, u, 0, u, v, (float)(base + along), color, color, color, color);
    } else {
        ((BorderStripFn)&gx_draw_sprite_strip4)(
            (double)FLOAT_8061b2f4,
            perp2, (double)(float)(a2 - along), perp1, (double)(float)(a2 - along),
            perp2, (double)(float)(base + along), perp1,
            0, 0, u, 0, 0, v, u, v, (float)(base + along), color, color, color, color);
    }
    FUN_8038273c();
}

// -------------------------------------------------------------------------
// 0x80369004  grid_minigame_wall_face_spark - set a wall face index + spawn spark.
//   For a given wall direction (dir), rotate it by the tile pose, write the
//   resulting face slot index into the face table, then spawn a fading spark
//   coloured by the wall kind (green for kind 0, red otherwise).
// -------------------------------------------------------------------------
void grid_minigame_wall_face_spark(int dir, int inst, int idx, int faceTab, int matId, int cell)
{
    (void)inst; (void)idx;
    math_poly_trig((double)FLOAT_8061b290);
    math_poly_trig((double)FLOAT_8061b294);

    int face;
    if (dir == 2) {
        face = matId + 3;
    } else if (dir < 2) {
        face = matId;
        if (dir != 0) { face = -1; if (0 <= dir) face = matId + 2; }
    } else {
        face = -1;
        if (dir < 4) face = matId + 1;
    }
    if (face != -1) {
        *(int*)(faceTab + face * 0x30) = cell + 2;
    }

    spark_spawn_fading();
    spark_set_last_scale((double)FLOAT_8061b2c4);
    spark_set_last_lifetime((double)FLOAT_8061b2c4);
    spark_set_last_color((idx == 0) ? 0x7f7f2828u : 0x7f287f28u);   // (idx here is the kind arg)
}

// -------------------------------------------------------------------------
// 0x803691b0  grid_minigame_glow_lighting - per-cell proximity glow lighting.
//   Registered as the tile entity's skinned-lighting callback. For a placed
//   grid cell (owner id high word 0x5050) it scales the base light colour, then
//   brightens it toward any lit neighbour walls and toward the cursor cell,
//   clamps each channel, and writes the resulting light colour back into the
//   entity's material light slot. param_3 receives the (zeroed) ambient term.
// -------------------------------------------------------------------------
void grid_minigame_glow_lighting(int a1, int a2, float* outAmbient)
{
    (void)a1; (void)a2;
    unsigned long long packed = (unsigned long long)FUN_803826e8();
    int self = (int)(packed >> 0x20);
    float* light = (float*)(unsigned int)packed;

    if ((*(unsigned int*)(self + 0x60) & 0xffff0000) == 0x50500000) {
        light[0] *= FLOAT_8061b330;
        light[1] *= FLOAT_8061b330;
        light[2] *= FLOAT_8061b330;

        unsigned int col = *(unsigned int*)(self + 0x60) >> 8 & 0xff;
        unsigned int row = *(unsigned int*)(self + 0x60) & 0xff;
        int r0 = (int)col - 1; if (r0 < 0) r0 = 0;
        int rowOff = r0 * 0x14;

        for (int rr = r0; ; rr++, rowOff += 0x14) {
            int rMax = (int)(DAT_805ec318 - 1);
            if ((int)(col + 1) < rMax) rMax = col + 1;
            if (rMax < rr) break;
            int c0 = (int)row - 1; if (c0 < 0) c0 = 0;
            int colOff = c0 * 0x8c;
            for (int cc = c0; ; cc++, colOff += 0x8c) {
                int cMax = (int)(DAT_805ec31a - 1);
                if ((int)(row + 1) < cMax) cMax = row + 1;
                if (cMax < cc) break;
                unsigned int walls = *(unsigned int*)((int)&DAT_805ec318 + rowOff + colOff + 0x10);
                if (walls != 0) {
                    float k = FLOAT_8061b314;
                    if (((unsigned int)rr == col) && ((unsigned int)cc == row)) k = FLOAT_8061b2d8;
                    if (((unsigned int)rr != col) && ((unsigned int)cc != row)) k = FLOAT_8061b2b0;
                    if (walls & 0xf)  light[2] += FLOAT_8061b330 / k;
                    if (walls & 0xf0) light[1] += FLOAT_8061b330 / k;
                }
            }
        }

        if ((col == (unsigned int)DAT_80612eac) && (row == (unsigned int)uRam80612eb0)) {
            light[0] += FLOAT_8061b2ac;
            light[1] += FLOAT_8061b330;
            light[2] += FLOAT_8061b330;
        }

        outAmbient[0] = FLOAT_8061b27c;
        outAmbient[1] = FLOAT_8061b27c;
        outAmbient[2] = FLOAT_8061b27c;

        float bias = FLOAT_8061b278;
        light[0] += bias; light[1] += bias; light[2] += bias;

        // Clamp each channel into [0, FLOAT_8061b338].
        for (int ch = 0; ch < 3; ch++) {
            float val = light[ch];
            float hi = (FLOAT_8061b27c < val) ? val : FLOAT_8061b27c;
            float out = FLOAT_8061b338;
            if ((hi < FLOAT_8061b338) && (FLOAT_8061b27c < val)) out = val;
            else if (hi < FLOAT_8061b338) out = FLOAT_8061b27c;
            light[ch] = out;
        }

        // Compose with the entity's current material light colour.
        int rig = *(int*)(self + 4);
        float m0 = *(float*)(rig + 0x54) * FLOAT_8061b33c;
        float m1 = *(float*)(rig + 0x58) * FLOAT_8061b33c;
        float m2 = *(float*)(rig + 0x5c) * FLOAT_8061b33c;
        light[0] = FLOAT_8061b2f0 * light[0] + m0;
        light[1] = FLOAT_8061b2f0 * light[1] + m1;
        light[2] = FLOAT_8061b2f0 * light[2] + m2;
        *(float*)(*(int*)(self + 4) + 0x54) = light[0];
        *(float*)(*(int*)(self + 4) + 0x58) = light[1];
        *(float*)(*(int*)(self + 4) + 0x5c) = light[2];
    }
    FUN_80382734();
}

// ============================================================================
// Address range: 0x8037aca4 - 0x8037b34c | batch_1055 | 2026-09-13
//
// Ekun grid minigame - the interactive front end for the board built above.
// This block runs the live minigame reached from the grid2 confirm menu: it
// hit-tests the two on-screen icon buttons, runs the RNG "auto-play" cursor
// that drifts a target dot around inside the current board bounds each frame,
// normalises the player/cursor entity position into board-relative [0,1] uv,
// spawns the two prop UI instances (the page arrows + the board panel) with
// their per-frame draw/tick callbacks, and on confirm resolves the picked
// grid result (TOSS / POW / PWRD / DONE tags) into owner-slot record edits and
// advances the stat-level to the next board.
//
//   grid_minigame_buttons_hit_test   0x8037aca4  hit-test the two icon buttons
//   grid_minigame_reset_boards       0x8037ad00  load default board + board 1
//   grid_minigame_render_active_lines 0x8037ad28 render lines for the active board
//   grid_minigame_autoplay_tick      0x8037ad64  RNG auto-play cursor drift + clamp
//   grid_minigame_check_active       0x8037afd0  1 when mode==0x16 and no worm result
//   grid_minigame_get_cursor_norm    0x8037b010  entity pos -> board-relative uv
//   grid_minigame_finish_and_advance 0x8037b05c  dismount, complete stat, next board
//   grid_minigame_apply_result_tags  0x8037b0f4  apply grid result -> owner slots
//   grid_minigame_panel_confirm      0x8037b21c  panel confirm: reveal + drag cooldown
//   grid_minigame_spawn_ui           0x8037b34c  spawn the arrows + board prop UI
//
// NOTE: non-matching decomp. The auto-play tick reproduces the original's
// paired-single / CONCAT44 integer->float RNG conversion as equivalent float
// expressions; the board-bounds pointer DAT_80612f98 is a float[4] {x0,y0,x1,y1}
// and DAT_80612f94 is the tracked cursor entity. Control flow, side effects,
// entity/tile types, and the four-way result-tag branch match the original.

// ---- live minigame state (0x80612f7x .. 0x80612fax) -----------------------
extern int    DAT_80611358;          // last-ticked frame stamp
extern int    DAT_80611928;          // current frame stamp
extern int    DAT_80611d74;          // active mounted entity (dismount target)
extern int    DAT_80612614;          // current mode id (0x16 == grid minigame)
extern int    DAT_80612f90;          // auto-play "just re-seeded" flag
extern int    DAT_80612f94;          // tracked cursor entity ptr
extern float *DAT_80612f98;          // board bounds float[4] {x0,y0,x1,y1}
extern int    DAT_80612f9c;          // panel confirm scratch flag
extern int    DAT_80612fa4;          // spawn-once guard for the prop UI
extern float  FLOAT_80612f78, fRam80612f7c;   // auto-play velocity xy
extern float  FLOAT_80612f80, fRam80612f84;   // auto-play accumulated drift xy
extern float  FLOAT_80612f88, fRam80612f8c;   // auto-play clamped target xy
extern int    DAT_805ee3b8, DAT_805ee3e8;     // icon button records (float[])
extern int    DAT_804971a4, DAT_804971a8, DAT_804971ac, DAT_804971b0; // panel rect
// ---- rodata float constants (this block) ----------------------------------
extern float  FLOAT_80612f88;
extern float  FLOAT_806108ec;        // auto-play step gain
extern float  FLOAT_8061b6c8, FLOAT_8061b6e8, FLOAT_8061b700; // board prop spawn coords
extern double DOUBLE_8061b6e0;       // 2^52 bias for the int->float RNG conv (frame)
extern float  FLOAT_8061b6f0, FLOAT_8061b73c;  // target clamp lo / hi
extern double DOUBLE_8061b740;       // 2^52 bias for the int->float RNG conv (rng)
extern float  FLOAT_8061b728, FLOAT_8061b72c, FLOAT_8061b730, FLOAT_8061b734, FLOAT_8061b738;
extern float  FLOAT_8061b748, FLOAT_8061b74c, FLOAT_8061b750, FLOAT_8061b754;
// ---- engine callees --------------------------------------------------------
// Most engine callees (prop_*, statlvl_*, icon_button_hit_test, worm_results_*,
// entity_handle_resolve, get_string_by_id, prop_instance_spawn,
// prop_set_scale_vec, prop_page_arrows_draw, grid_minigame_reset_boards) resolve
// through generated_decls.h - do NOT re-declare them here.  Only the raw FUN_
// scratch helpers not covered by a generated prototype are declared locally.
extern int  FUN_8038b5c8(int str);               // measure/format string -> width
extern void FUN_80382734(void);                  // scratch leave (panel)
extern void FUN_8038273c(void);                  // scratch leave (spawn)
// NOTE: FUN_803826f0 is already declared (unsigned long long) in the batch_1032
// header block above; reuse that declaration and take the low word as the ptr.
extern int  DAT_806109c0;                        // frame counter (time-limit calc)
// rng_counter_tick and FUN_803826e8 return a value the generated prototypes drop
// (declared void / int there). Call them through typed function-pointer casts to
// their real DOL addresses (via these macros) so this file can read the returned
// register (pair) without clashing with the shared header's declarations.
#define GRID_RNG_TICK()       ( ((unsigned int (*)(void))0x8013104c)() )       /* rng_counter_tick */
#define GRID_PANEL_SCRATCH()  ( ((unsigned long long (*)(void))0x803826e8)() ) /* FUN_803826e8 */

// Forward decls for intra-batch callbacks / callees used before definition.
void grid_minigame_render_active_lines(void);
void grid_minigame_autoplay_tick(double px, double py, double tx, double ty,
                                  double dx, double dy, double gain, double step,
                                  int ent, float *bounds, int frameArg);
void grid_minigame_panel_confirm(void);

// Hit-test both on-screen icon buttons against the current cursor (mx,my).
void grid_minigame_buttons_hit_test(int mx, int my)
{
    icon_button_hit_test((float *)&DAT_805ee3b8, mx, my);
    icon_button_hit_test((float *)&DAT_805ee3e8, mx, my);
}

// Reset the board back to the fixed default plus board template 1.
void grid_minigame_reset_boards(void)
{
    grid_minigame_load_default_board();
    grid_minigame_load_board(1);
}

// Render the grid lines for the active board using its bounds rect.
void grid_minigame_render_active_lines(void)
{
    if (DAT_80612f98 != (float *)0) {
        grid_minigame_render_lines((double)DAT_80612f98[0], (double)DAT_80612f98[1],
                                   (double)DAT_80612f98[2], (double)DAT_80612f98[3]);
    }
}

// Per-frame RNG "auto-play": once per re-arm interval it re-seeds a random
// velocity, then integrates the target dot and clamps it inside the board.
// The clamped target is stored at ent+0x38/+0x3c when the entity isn't mounted
// and there is no active worm result.
void grid_minigame_autoplay_tick(double px, double py, double tx, double ty,
                                  double dx, double dy, double gain, double step,
                                  int ent, float *bounds, int frameArg)
{
    unsigned int lead;
    int mounted;
    unsigned int r;

    if (DAT_80611358 != DAT_80611928) {
        // (3 - frameArg) >= 0  ->  flag 1 ; encoded via clz>>5 in the original.
        lead = countLeadingZeros(3 - frameArg);
        DAT_80612f90 = lead >> 5;
        DAT_80612f94 = ent;
        DAT_80612f98 = bounds;

        mounted = prop_entity_is_mounted(ent);
        if (mounted == 0) {
            r = GRID_RNG_TICK();
            if (r == (r / (unsigned int)DAT_806109c0) * (unsigned int)DAT_806109c0) {
                // Re-seed a fresh random velocity/drift this interval.
                r = GRID_RNG_TICK();
                FLOAT_80612f80 = FLOAT_8061b728 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061b740) / FLOAT_8061b72c
                     - FLOAT_8061b730);
                r = GRID_RNG_TICK();
                fRam80612f84 = FLOAT_8061b728 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061b740) / FLOAT_8061b72c
                     - FLOAT_8061b730);
                r = GRID_RNG_TICK();
                FLOAT_80612f78 = FLOAT_8061b734 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061b740) / FLOAT_8061b72c
                     - FLOAT_8061b730);
                r = GRID_RNG_TICK();
                DAT_80612f90 = 1;
                fRam80612f7c = FLOAT_8061b734 *
                    ((float)((double)CONCAT44(0x43300000, r) - DOUBLE_8061b740) / FLOAT_8061b72c
                     - FLOAT_8061b730);
            }

            step = (double)FLOAT_806108ec;
            gain = (double)(FLOAT_8061b738 /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061b6e0));

            FLOAT_80612f80 = (float)(step * (double)(float)((double)FLOAT_80612f78 * gain)
                                     + (double)FLOAT_80612f80);
            ty = (double)FLOAT_80612f80;
            fRam80612f84 = (float)(step * (double)(float)((double)fRam80612f7c * gain)
                                   + (double)fRam80612f84);
            tx = (double)fRam80612f84;
            dy = (double)(float)(ty * gain);
            py = (double)(float)(tx * gain);

            FLOAT_80612f88 = (float)(step * dy + (double)FLOAT_80612f88);
            dx = (double)FLOAT_80612f88;
            fRam80612f8c = (float)(step * py + (double)fRam80612f8c);

            if (dx < (double)FLOAT_8061b6f0)  FLOAT_80612f88 = FLOAT_8061b6f0;
            if (FLOAT_8061b73c < FLOAT_80612f88) FLOAT_80612f88 = FLOAT_8061b73c;
            if (fRam80612f8c < FLOAT_8061b6f0) fRam80612f8c = FLOAT_8061b6f0;
            px = (double)fRam80612f8c;
            if ((double)FLOAT_8061b73c < px) fRam80612f8c = FLOAT_8061b73c;
        }
        grid_minigame_tick_fwd((unsigned long long)0, ty, dx, dy, tx, dy, gain, step);
        DAT_80611358 = DAT_80611928;
    }

    mounted = prop_entity_is_mounted(ent);
    if ((mounted == 0) && (worm_results_get_state() == -1)) {
        *(float *)(ent + 0x38) = FLOAT_80612f88;
        *(float *)(ent + 0x3c) = fRam80612f8c;
    }
}

// True (1) while the grid minigame mode is live (id 0x16) and there is not yet
// a resolved worm result.
unsigned int grid_minigame_check_active(void)
{
    unsigned int active;
    if (DAT_80612614 == 0x16) {
        int st = worm_results_get_state();
        active = (unsigned int)countLeadingZeros(-1 - st) >> 5;
    } else {
        active = 0;
    }
    return active;
}

// Convert the tracked cursor entity's world position into board-relative uv in
// [0,1] and copy out the current auto-play re-seed flag.
void grid_minigame_get_cursor_norm(float *outU, float *outV, unsigned int *outFlag)
{
    float ex = *(float *)(DAT_80612f94 + 0x38);
    float ey = *(float *)(DAT_80612f94 + 0x3c);
    *outFlag = (unsigned int)DAT_80612f90;
    float by0 = DAT_80612f98[1];
    float by1 = DAT_80612f98[3];
    *outU = (ex - DAT_80612f98[0]) / (DAT_80612f98[2] - DAT_80612f98[0]);
    *outV = (ey - by0) / (by1 - by0);
}

// Confirm the grid result: dismount the active board entity, run the stat-level
// completion for the current class, and load the follow-up board (2 on win, 5
// otherwise).
void grid_minigame_finish_and_advance(unsigned long long a1, unsigned long long a2,
                                      unsigned long long a3, unsigned long long a4,
                                      unsigned long long a5, unsigned long long a6,
                                      unsigned long long a7, unsigned long long a8)
{
    if ((DAT_80611d74 != 0) && (*(int *)(DAT_80611d74 + 0x114) == 0x2000)) {
        int mount = prop_player_mount_entity(DAT_80611d74);
        prop_player_dismount(mount);
    }
    if (statlvl_class_is_state5(0xb) == 0) {
        statlvl_class_complete(a2, (double)FLOAT_8061b748, (double)a3,
                               (long long)a4, (long long)a5, (long long)a6,
                               (long long)a7, (long long)a8);
        grid_minigame_load_board(2);
    } else {
        // Both the state-7 and default branches complete the same class; the
        // original kept the split for a (removed) side effect - preserved here.
        statlvl_class_is_state5(7);
        statlvl_class_complete(a2, (double)FLOAT_8061b748, (double)a3,
                               (long long)a4, (long long)a5, (long long)a6,
                               (long long)a7, (long long)a8);
        grid_minigame_load_board(5);
    }
}

// Apply the picked grid cell's result tag to the resolved owner's slot records.
// The four-char tags (TOSS/POW /NOP /DONE/PWRD/PWR2/DON2) select which of two
// slot indices gets its "active" bit cleared and its entry re-activated.
void grid_minigame_apply_result_tags(int *panel)
{
    int rec = *panel;
    grid_minigame_load_board(*(int *)(rec + 0xc));

    int owner;
    if ((*(int *)(rec + 0xac) != 0) &&
        ((owner = entity_handle_resolve(*(unsigned int *)(rec + 0x14))) != 0) &&
        ((owner = *(int *)(owner + 0x7c)) != 0)) {
        int slotA = (*(int *)(rec + 0x14) == 0x544f5353) ? 5 : 7;   // 'TOSS'
        int slotB = (*(int *)(rec + 0x14) == 0x544f5353) ? 4 : 6;
        int tag = *(int *)(rec + 0xac);

        if ((tag == 0x4e4f5057) || (tag + 0xb1b0afceU < 2) || (tag == 0x444f4e45)) {
            int slot = prop_owner_slot_record(owner, slotA);
            *(unsigned int *)(slot + 0x14) &= 0xfffffffd;
            prop_activate_entry();
        } else if ((tag == 0x50575244) || (tag == 0x50575232) || (tag == 0x444f4e32)) {
            prop_activate_entry();
            int slot = prop_owner_slot_record(owner, slotB);
            *(unsigned int *)(slot + 0x14) &= 0xfffffffd;
        }
        *(int *)(owner + 0x44) = 0;
    }
}

// Panel confirm handler: reveal the result panel, format the prompt string,
// apply the drag cooldown, and (when the accumulated drag passes the string
// width threshold) clear the panel's active bit and re-activate its entry.
void grid_minigame_panel_confirm(void)
{
    unsigned long long pair = GRID_PANEL_SCRATCH();
    int ctx = (int)((unsigned long long)pair >> 0x20);
    float *rect = (float *)pair;

    int str = get_string_by_id(0x1135 - (unsigned int)(**(int **)(ctx + 0x10) == 0x544f5353));
    int width = FUN_8038b5c8(str);

    DAT_804971a4 = (int)rect[0];
    DAT_804971a8 = (int)rect[2];
    DAT_804971ac = (int)rect[1];
    DAT_804971b0 = (int)rect[3];
    prop_panel_reveal_tick();
    DAT_80612f9c = 0;

    if (width <= *(int *)(ctx + 0x44)) {
        rect[5] = (float)((unsigned int)rect[5] & 0xfffffffd);
        prop_set_drag_cooldown(ctx);
        prop_activate_entry();
    }
    FUN_80382734();
}

// Spawn the two prop instances that make up the grid minigame UI: the page
// arrows (draw = prop_page_arrows_draw, confirm = grid_minigame_panel_confirm)
// and the board panel (draw = render lines, tick = auto-play). Guarded so it
// only runs once (DAT_80612fa4).
void grid_minigame_spawn_ui(void)
{
    int self = (int)FUN_803826f0();
    *(unsigned short *)(self + 0x1a) |= 1;
    *(unsigned short *)(self + 0x1a) |= 2;
    *(unsigned short *)(self + 0x1a) |= 4;
    *(int *)(self + 0x20) = 0;

    int slotArrows, slotBoard;
    if (DAT_80612fa4 == 0) {
        grid_minigame_reset_boards();
        slotArrows = 5;
        slotBoard = 4;
    } else {
        slotArrows = 7;
        slotBoard = 6;
    }

    // Callback pointers are passed to prop_set_scale_vec as raw DOL addresses
    // (the generated prototype types them as int); recBase 0x805ee538 is the
    // shared prop instance record template.
    prop_instance_spawn((double)FLOAT_8061b700, (double)FLOAT_8061b74c, (double)FLOAT_8061b6e8,
                        (double)FLOAT_8061b750, 0x805ee538, self, slotArrows, 6.15871e-42f, 0.0f, 0.0f);
    int rec = prop_owner_slot_record(self, slotArrows);
    prop_set_scale_vec(rec, 0x8037a604 /*prop_page_arrows_draw*/,
                       0x8037b21c /*grid_minigame_panel_confirm*/, 0);
    prop_set_field14(rec, 3);

    prop_instance_spawn((double)FLOAT_8061b6c8, (double)FLOAT_8061b6c8, (double)FLOAT_8061b754,
                        (double)FLOAT_8061b754, 0x805ee538, self, slotBoard, 6.55527e-42f, 0.0f, 0.0f);
    rec = prop_owner_slot_record(self, slotBoard);
    prop_set_scale_vec(rec, 0x8037ad28 /*grid_minigame_render_active_lines*/,
                       0x8037ad64 /*grid_minigame_autoplay_tick*/, 0);
    prop_set_field14(rec, 1);

    DAT_80612fa4 = 1;
    FUN_8038273c();
}
