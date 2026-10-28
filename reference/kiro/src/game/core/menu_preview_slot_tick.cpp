// ============================================================================
// menu_preview_slot_tick.cpp
// Batch: batch_813
// Address range: 0x802d24a0 - 0x802d3f5c, 0x802d40f0 - 0x802d41fc, 0x802d4ea0
// Date: 2026-09-11T02:35:00Z
//
// Front-end weapon/character preview screen: per-slot spawn, per-frame input
// tick (weapon cycling, team select, ready-up state machine, muzzle/particle
// sizing, sound), the top-level ready-check tick, the split-screen divider
// render, and the screen build + enter entry points. This is the interactive
// layer on top of the preview-bot subsystem in menu_preview_bots.cpp.
//
// Per-slot storage (slot index s):
//   DAT_805cca28 + s*0x16c   per-slot select/anim state block
//   DAT_805cca48 + s*0x5b*4  per-slot spin/anim float block
//   DAT_805cc920 + s*0x16c   weapon-selection record (type/subtype/variant)
//   DAT_80612b18 + s         per-slot UI mode byte (0..6 state machine)
//   DAT_80612b10 + s*2       saved previous UI mode (for back-out)
//
// NON-MATCHING: incoming paired-single FP spill regs are modelled via explicit
// locals; __save_gpr_28 / frame-actor thunk pairs return a 64-bit pair split by
// shifts; CONCAT44 magic-bias int->float conversions are kept verbatim. All raw
// per-slot state is addressed through unsigned-char base symbols with the exact
// byte offsets emitted by the decompiler (faithful, compilable).
// ============================================================================

#include "common.h"
#include "menu.h"     // pulls generated_decls.h (named cross-module helpers)
#include "hud.h"
#include "sound.h"
#include "entity.h"
#include "weapon.h"
#include "resource.h"
#include "render.h"
#include "character.h"

// ---- frame / prologue helper thunks ---------------------------------------
extern "C" long long          __save_gpr_28(void);
extern "C" int                __save_gpr_25(void);
extern "C" void               __restore_gpr_28(void);
extern "C" void               __restore_gpr_25(void);
extern "C" int                FUN_803826dc(void);   // frame-actor accessor (hud element)
extern "C" int                FUN_803826c0(void);   // preview screen frame push
extern "C" void               FUN_80382728(void);   // frame-actor tail (tick)
extern "C" void               FUN_8038270c(void);   // frame-actor tail (build)
extern "C" void               FUN_8035b0e4(void);   // modifier apply pre-pass
extern "C" void               FUN_8035b0f8(int, int, int, int);

// ---- ABI-bound helpers (generated decls have wrong arity/return) ----------
// entity_spawn_by_type at 0x80072d70 takes 13 args and returns the entity ptr;
// the generated prototype is a 4-arg void, so bind by address here (matches the
// pattern in menu_preview_bots.cpp).
typedef int (*spawntype_fp)(int, double, double, unsigned int, unsigned int, unsigned int,
                            unsigned int, unsigned int, int, void *, void *, int, int);
static const spawntype_fp __mp_spawn_type = (spawntype_fp)0x80072d70;

// All named cross-module helpers (hud_*, input_*, anim_*, sound_*, weapon_*,
// menu_preview_*, gx_*, viewport_*, award_*, mode_*, particle_*, memset_wrapper,
// get_string_by_id, ...) resolve through generated_decls.h, pulled in by the
// shim includes below. Per the compile-clean standard we do NOT redeclare them.

// ---- raw scalar ROM globals ------------------------------------------------
extern "C" int   DAT_80612b40;   // running slot index during spawn/build
extern "C" int   DAT_80611014;   // active preview slot count
extern "C" int   DAT_806119ec;   // current build slot index / layout mode
extern "C" int   DAT_806119e0;   // layout context handle
extern "C" int   DAT_80612acc;   // shared extra preview entity (mode2)
extern "C" int   DAT_80612ad0;   // options-vs-loadout flag
extern "C" int   DAT_805cc614;   // active game mode id
extern "C" int   DAT_805cc628;   // worm-mode object id
extern "C" char  DAT_805cc68e;   // mode sub-variant byte
extern "C" int   DAT_805cc4b4;   // weapon list length
extern "C" int   DAT_805cc4b8;   // weapon column count
extern "C" int   DAT_80612b3c;   // computed column pixel width
extern "C" int   DAT_80612b38;   // mode-7 special preview entity
extern "C" int   DAT_806109c4;   // screen width
extern "C" int   DAT_806109c8;   // screen height
extern "C" int  *DAT_80611748;   // active viewport rect ptr (int[7])
extern "C" int   DAT_806125fc;   // resolved gameplay modifier mask
extern "C" unsigned int DAT_806109c0;  // frame time / divisor
extern "C" int   DAT_806108e8;   // frame delta (int view)

// ---- raw base symbols addressed via &base + byte offset -------------------
extern "C" unsigned char DAT_805cca28;   // per-slot state block base [s*0x16c]
extern "C" unsigned char DAT_805cca2c;
extern "C" unsigned char DAT_805cca30;
extern "C" unsigned char DAT_805cca34;
extern "C" unsigned char DAT_805cca35;
extern "C" unsigned char DAT_805cca36;
extern "C" unsigned char DAT_805cca37;
extern "C" unsigned char DAT_805cca38;
extern "C" unsigned char DAT_805cca3c;
extern "C" unsigned char DAT_805cca40;
extern "C" unsigned char DAT_805cca44;
extern "C" unsigned char DAT_805cca48;   // per-slot float block base [s*0x5b*4]
extern "C" unsigned char DAT_805cca4c;
extern "C" unsigned char DAT_805cca50;
extern "C" unsigned char DAT_805cca54;
extern "C" unsigned char DAT_805cca58;
extern "C" unsigned char DAT_805cca5c;
extern "C" unsigned char DAT_805cca60;
extern "C" unsigned char DAT_805cca64;
extern "C" unsigned char DAT_805cca68;
extern "C" unsigned char DAT_805cca6c;
extern "C" unsigned char DAT_805cca74;   // slot pointer block base [s*0x5b*4]
extern "C" unsigned char DAT_805cca88;
extern "C" unsigned char DAT_805cc920;   // weapon record base [s*0x16c]
extern "C" unsigned char DAT_805cc924;
extern "C" unsigned char DAT_805cc900;   // per-mode team assignment table
extern "C" unsigned char DAT_80612b10;   // saved previous UI mode [s*2]
extern "C" unsigned char DAT_80612b18;   // per-slot UI mode byte [s]
extern "C" unsigned char DAT_805cc4fc;   // per-column weapon count table
extern "C" unsigned char DAT_805cbf40;   // resolved weapon type list
extern "C" unsigned char DAT_805cc3f0;   // resolved weapon subtype list
extern "C" unsigned char DAT_805cc198;   // resolved weapon name-string list
extern "C" unsigned char DAT_805cc488;   // team index table (int[4])
extern "C" unsigned char DAT_805cc498;   // channel index table (int[7])
extern "C" unsigned char DAT_8047467c;   // weapon-group enable table stride 0x30
extern "C" unsigned char DAT_80474680;   // weapon-group entry (map id) stride 0x30
extern "C" unsigned char DAT_80474688;   // weapon-group entry (name string) stride 0x30
extern "C" unsigned char DAT_8047467d;   // weapon-group entry (subtype) stride 0x30
extern "C" unsigned char DAT_804746ac;   // weapon-group terminator byte stride 0x30
extern "C" unsigned char DAT_805cc4bc;   // per-group base index
extern "C" unsigned char DAT_805cc4bd;   // per-group base index (write cursor)
extern "C" unsigned char DAT_805cc4fd;   // per-group count (write cursor)
extern "C" unsigned char DAT_80473d68;   // weapon-behavior flags table stride 0x14
extern "C" unsigned char DAT_80473d64;   // weapon secondary-anim table stride 0x14
extern "C" unsigned char DAT_805328e0;   // slot render-param table base

// ---- float / double constants ---------------------------------------------
extern "C" float  FLOAT_80619ee8; // 1.0f
extern "C" float  FLOAT_80619eec; // aspect factor A
extern "C" float  FLOAT_80619f08; // 0.0f
extern "C" float  FLOAT_80619f0c; // 255.0f (alpha scale)
extern "C" float  FLOAT_80619f4c; // sound fade rate
extern "C" float  FLOAT_80619f5c; // confirm timer reset
extern "C" float  FLOAT_80619f60; // layout Y offset A
extern "C" float  FLOAT_80619f84; // spin accel base
extern "C" float  FLOAT_80619f88; // sound fade-in
extern "C" float  FLOAT_80619fac; // layout Y offset B
extern "C" float  FLOAT_80619ff4; // particle scale B
extern "C" float  FLOAT_8061a008; // particle scale A
extern "C" float  FLOAT_8061a024; // column width scale
extern "C" float  FLOAT_8061a090; // column width (2p)
extern "C" float  FLOAT_8061a0b4; // anim rate default
extern "C" float  FLOAT_8061a0b8; // analog deadzone
extern "C" float  FLOAT_8061a0bc; // scroll ease rate
extern "C" float  FLOAT_8061a0c0; // scroll ease rate Z
extern "C" float  FLOAT_8061a0c4; // column width (1p)
extern "C" float  FLOAT_8061a0c8; // column width (3p)
extern "C" float  FLOAT_8061a0cc; // column width base
extern "C" float  FLOAT_80611a64; // layout scale
extern "C" float  FLOAT_80611a6c; // layout bias
extern "C" float  FLOAT_806108e8; // frame delta
extern "C" double DOUBLE_80619f00; // 4503599627370496.0 magic bias

// ---- per-slot item builder vtable / settings-init jump table --------------
extern "C" unsigned char PTR_FUN_80476484;
extern "C" unsigned char PTR_LAB_804764a4;

// ---- helper: signed column count from slot count (5-clamp branch) ----------
static inline int preview_col_count(unsigned int cnt)
{
    if ((int)cnt < 3)
        return 5 - ((int)(((int)(cnt ^ 1) >> 1) - ((cnt ^ 1) & cnt)) >> 0x1f);
    return 3;
}

// forward decls (cross-referenced within this batch)
void menu_preview_slot_spawn_entity(int def, double x, double y, unsigned int a4,
                                    unsigned int a5, unsigned int a6, unsigned int a7,
                                    unsigned int a8);
void menu_preview_slot_input_tick(void);
void menu_preview_ready_check_tick(void);
void menu_preview_render_split_dividers(int elem);
void menu_preview_screen_build(void);
void menu_preview_screen_enter(unsigned int a0, double a1, double a2, unsigned int a3,
                               unsigned int a4, unsigned int a5, unsigned int a6,
                               unsigned int a7);

// ===========================================================================
// 0x802d24a0  menu_preview_slot_spawn_entity
// Spawn the live preview character for the next slot, pick its weapon from the
// current mode, initialise per-slot scroll/anim state, add a positioned HUD
// list item, then advance the running slot index.
// ===========================================================================
void menu_preview_slot_spawn_entity(int def, double x, double y, unsigned int a4,
                                    unsigned int a5, unsigned int a6, unsigned int a7,
                                    unsigned int a8)
{
    (void)def;
    int listItem = (int)__save_gpr_28();
    int block = (int)savegame_get_player_block(DAT_80612b40);

    float f = (DAT_80611014 < 3) ? FLOAT_80619ee8 : FLOAT_80619eec;
    double fontScale = (double)f;
    int font = (int)hud_style_get_font();
    fontScale = (double)(float)(fontScale * (double)*(float *)(font + 0x80));

    f = (DAT_80611014 == 1) ? FLOAT_80619fac : FLOAT_80619f60;
    double yOffset = (double)f;

    int s16c = DAT_80612b40 * 0x16c;
    *(int *)(&DAT_805cca28 + s16c) = 0;
    memset_wrapper(s16c + -0x7fa336d8, 0, 0x100);

    // menu_preview_slot_set_weapon is void; the entity type used for the spawn
    // is the weapon type resolved here (the decompiler's reused return slot).
    int weapon;
    if (DAT_805cc614 == 7 || DAT_805cc614 == 6) {
        unsigned char *obj = worm_mode_get_object(DAT_805cc628);
        weapon = **(int **)(obj + 0x6c);
        menu_preview_slot_set_weapon(DAT_80612b40, weapon, -1, 1);
    } else if (DAT_805cc614 == 9 && DAT_805cc68e == 2) {
        weapon = (int)beam_fx_get_segment_count();
        menu_preview_slot_set_weapon(DAT_80612b40, weapon, -1, 1);
    } else {
        weapon = *(int *)(block + 0x60);
        menu_preview_slot_set_weapon(DAT_80612b40, weapon, *(int *)(block + 0x64), 1);
    }

    if (DAT_80612acc == 0) {
        float local_58 = FLOAT_80619f08;
        float local_54 = FLOAT_80619f08;
        float local_50 = FLOAT_80619f08;
        (void)local_54; (void)local_50;
        DAT_80612acc = __mp_spawn_type(weapon, x, y, a4, a5, a6, a7, a8,
                                       0x43, &local_58, &local_58, 0, 0);
        *(unsigned char **)(DAT_80612acc + 0x254) = &DAT_805328e0;
        *(unsigned int *)(DAT_80612acc + 0x28) |= 0x40000000;
    }

    double posBias = DOUBLE_80619f00;
    int slot = DAT_80612b40;
    unsigned int mode = (unsigned int)DAT_80611014;
    block = DAT_80612b40 * 0x16c;
    bool leftHalf = (DAT_80611014 < 3);

    unsigned int magA = *(unsigned int *)(block + *(int *)(&DAT_805cca28 + block) * 4 + -0x7fa336d8) ^ 0x80000000;
    *(float *)(&DAT_805cca64 + block) = (float)((double)CONCAT44(0x43300000, magA) - DOUBLE_80619f00);
    unsigned int magB = *(unsigned int *)(&DAT_805cca28 + block) ^ 0x80000000;
    *(float *)(&DAT_805cca68 + block) = (float)((double)CONCAT44(0x43300000, magB) - posBias);

    int colCount = leftHalf ? preview_col_count(mode) : 3;
    int colClamp = DAT_805cc4b8;
    if (colCount <= DAT_805cc4b8)
        colClamp = ((int)mode < 3) ? preview_col_count(mode) : 3;

    if (*(int *)(&DAT_805cca28 + block) < colClamp) {
        *(float *)(&DAT_805cca6c + block) = FLOAT_80619f08;
    } else {
        colCount = ((int)mode < 3) ? preview_col_count(mode) : 3;
        colClamp = DAT_805cc4b8;
        if (colCount <= DAT_805cc4b8)
            colClamp = ((int)mode < 3) ? preview_col_count(mode) : 3;

        if (*(int *)(&DAT_805cca28 + block) < DAT_805cc4b8 - colClamp) {
            *(float *)(&DAT_805cca6c + block) = *(float *)(&DAT_805cca68 + block) - FLOAT_80619ee8;
        } else {
            colCount = ((int)mode < 3) ? preview_col_count(mode) : 3;
            colClamp = DAT_805cc4b8;
            if (colCount <= DAT_805cc4b8)
                colClamp = ((int)mode < 3) ? preview_col_count(mode) : 3;
            magB = (unsigned int)(DAT_805cc4b8 - colClamp) ^ 0x80000000;
            *(float *)(&DAT_805cca6c + block) =
                (float)((double)CONCAT44(0x43300000, magB) - DOUBLE_80619f00);
        }
    }

    *(int *)(&DAT_805cca2c + block) = 0;
    f = FLOAT_80619f08;
    *(int *)(&DAT_805cca30 + block) = 3;
    *(float *)(&DAT_805cca3c + block) = f;
    *(float *)(&DAT_805cca48 + slot * 0x5b * 4) = f;
    *(float *)(&DAT_805cca4c + slot * 0x5b * 4) = f;
    *(int *)(&DAT_805cca40 + block) = 1;
    *(int *)(&DAT_805cca50 + block) = 1;
    *(int *)(&DAT_805cca54 + block) = 0;
    *(int *)(&DAT_805cca58 + block) = 0;
    *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 0;
    *(int *)(&DAT_805cca60 + slot * 0x5b * 4) = 0;
    *(&DAT_805cca34 + block) = 0xce;
    *(&DAT_805cca35 + block) = 0xce;
    *(&DAT_805cca36 + block) = 0xce;
    *(&DAT_805cca37 + block) = 0xce;
    *(int *)(&DAT_80612b10 + slot * 2 * 4) = 2;
    *(int *)(&DAT_805cca38 + block) = 0;
    (&DAT_80612b18)[slot] = 1;
    *(unsigned char *)(slot * 2 + -0x7f9ed4ef) = 2;

    hud_element_init_from_layout(0, 0, 0, 0);
    hud_add_list_item();
    posBias = (double)FLOAT_80619f08;
    *(float *)(listItem + 0x64) = FLOAT_80619f08;
    if (1 < DAT_806119ec) posBias = fontScale;
    leftHalf = (DAT_806119ec == 1 || DAT_806119ec == 3);
    if (!leftHalf) yOffset = (double)FLOAT_80619f08;
    hud_element_set_pos_xy(yOffset, posBias, DAT_806119e0);
    *(int *)(listItem + 0x38) = DAT_80612b40;
    DAT_80612b40 = DAT_80612b40 + 1;
    __restore_gpr_28();
}

// ===========================================================================
// 0x802d40f0  menu_preview_render_split_dividers
// Draw the split-screen divider lines (vertical for 2+ players, horizontal for
// 3+), alpha-blended by the frame-actor's fade value at +0x1b4.
// ===========================================================================
void menu_preview_render_split_dividers(int elem)
{
    viewport_set_rect_active(0, 0, DAT_806109c4 - 1, DAT_806109c8 - 1);
    gx_set_scissor_rect(*DAT_80611748, DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    viewport_set_gx_active();

    if (1 < DAT_80611014) {
        int cx = ((int)DAT_806109c4 >> 1) + (unsigned int)((int)DAT_806109c4 < 0 && (DAT_806109c4 & 1) != 0);
        gx_fill_rect_clipped(cx - 2, 0, cx + 2, DAT_806109c8,
                             (int)(FLOAT_80619f0c * *(float *)(elem + 0x1b4)) & 0xff);
    }
    if (2 < DAT_80611014) {
        int cy = (int)((*(float *)(*(int *)(elem + 0x1c) + 0x80) * FLOAT_80611a64 + FLOAT_80611a6c) *
                       FLOAT_80619eec);
        gx_fill_rect_clipped(0, cy - 2, DAT_806109c4, cy + 2,
                             (int)(FLOAT_80619f0c * *(float *)(elem + 0x1b4)) & 0xff);
    }
}

// ===========================================================================
// 0x802d3f5c  menu_preview_ready_check_tick
// Top-level per-frame tick: resolve the active gameplay-modifier mask, run the
// per-slot modifier apply pass, then check whether all slots have finished
// their ready sequence; if so exit the screen, otherwise handle a global back
// (L-trigger) that tears down the previews and pops the screen.
// ===========================================================================
void menu_preview_ready_check_tick(void)
{
    int elem = __save_gpr_25();
    bool allReady = true;

    unsigned int sid = gameplay_modifier_get_string_id();
    unsigned int mask = gameplay_modifier_get_mask();
    DAT_806125fc = mask & sid;
    FUN_8035b0e4();

    int s16c = 0;
    for (int i = 0; i < DAT_80611014; i++) {
        int arg = 0;
        char mode7 = menu_preview_is_mode7();
        int actor = DAT_80612b38;
        if (mode7 == 0) {
            arg = *(int *)(&DAT_805cca60 + s16c);
            actor = *(int *)(&DAT_805cca74 + s16c);
        }
        FUN_8035b0f8(1, i, actor, arg);
        s16c += 0x16c;
    }

    unsigned char *modeByte = &DAT_80612b18;
    s16c = 0;
    for (int i = 0; i < DAT_80611014; i++) {
        if (*modeByte != 6 || *(int *)(&DAT_805cca5c + s16c) != 5 ||
            sound_is_playing(*(unsigned int *)(*(int *)(&DAT_805cca74 + s16c) + 0x22c)) != 0) {
            allReady = false;
        }
        s16c += 0x16c;
        modeByte += 1;
    }

    if (allReady && hud_element_is_closing(elem) == 0) {
        menu_preview_screen_exit();
    } else {
        unsigned int held = input_get_buttons_held(0);
        if ((held & 0x10) != 0 && DAT_80612b18 == 2) {
            menu_preview_all_destroy();
            hud_screen_pop_current();
        }
    }
    __restore_gpr_25();
}

// ===========================================================================
// 0x802d4ea0  menu_preview_screen_enter  (thunk to menu_preview_system_init)
// ===========================================================================
void menu_preview_screen_enter(unsigned int a0, double a1, double a2, unsigned int a3,
                               unsigned int a4, unsigned int a5, unsigned int a6,
                               unsigned int a7)
{
    menu_preview_system_init(a0, a1, a2, a3, a4, a5, a6, a7);
}

// ===========================================================================
// 0x802d41fc  menu_preview_screen_build
// Build the preview screen: resolve the selectable weapon list from map-award
// completion (or the fixed worm/beam weapon for those modes), seed the team and
// channel index tables, then create one per-slot preview item and register the
// screen handler.
// ===========================================================================
void menu_preview_screen_build(void)
{
    unsigned int frame = (unsigned int)FUN_803826c0();
    int savedElem = hud_style_push_element(1);
    int savedVariant = hud_style_apply_variant(0);
    DAT_80612b40 = 0;

    float fw = FLOAT_8061a0c4;
    if (DAT_80611014 < 3) {
        fw = FLOAT_8061a0c8;
        if (1 < DAT_80611014) fw = FLOAT_8061a090;
    }
    DAT_805cc4b4 = 0;
    DAT_80612b3c = (int)(FLOAT_8061a0cc * FLOAT_8061a024 * fw);
    DAT_805cc4b8 = 0;

    if ((DAT_805cc614 == 7 || DAT_805cc614 == 6) ||
        (DAT_805cc614 == 9 && DAT_805cc68e == 2)) {
        int off = 0;
        for (int i = 0; i < DAT_80611014; i++) {
            int type;
            if (DAT_805cc614 == 7 || DAT_805cc614 == 6) {
                unsigned char *obj = worm_mode_get_object(DAT_805cc628);
                type = **(int **)(obj + 0x6c);
            } else {
                type = beam_fx_get_segment_count();
            }
            *(int *)(&DAT_805cbf40 + off) = type;
            unsigned char *rec = weapon_def_find_record(type, -1);
            *(int *)(&DAT_805cc198 + off) = *(int *)(rec + 0xc);
            off += 4;
            (&DAT_805cc3f0)[i] = 0xff;
            DAT_805cc4b4 = 1;
            DAT_805cc4b8 = 1;
            (&DAT_805cc4bc)[i] = 0;
            (&DAT_805cc4fc)[i] = 1;
        }
    } else {
        unsigned int grp = 0;
        int grpStride = 0;
        int lastCol = -1;
        do {
            int col = lastCol;
            if ((&DAT_8047467c)[grpStride] != 0) {
                bool skip = false;
                int eStride = grp * 0x30;
                unsigned int e = grp;
                do {
                    int done = award_check_map_completion(*(int *)(&DAT_80474680 + eStride),
                                                          (int)(char)(&DAT_8047467d)[eStride]);
                    if (done != 0 && mode_player_scored(*(int *)(&DAT_80474680 + eStride)) != 0)
                        goto found;
                    e = e + 1;
                } while (e < 0x96 && (*(char *)(&DAT_804746ac + eStride) == 0 ? (eStride += 0x30, true) : false));
                skip = true;
            found:
                if (!skip) {
                    col = lastCol + 1;
                    (&DAT_805cc4bd)[lastCol] = (char)DAT_805cc4b4;
                    (&DAT_805cc4fd)[lastCol] = 0;
                    eStride = e * 0x30;
                    do {
                        int done = award_check_map_completion(*(int *)(&DAT_80474680 + eStride),
                                                              (int)(char)(&DAT_8047467d)[eStride]);
                        if (done != 0 && mode_player_scored(*(int *)(&DAT_80474680 + eStride)) != 0) {
                            int nameStr = *(int *)(&DAT_80474688 + eStride);
                            (&DAT_805cbf40)[DAT_805cc4b4] = *(int *)(&DAT_80474680 + eStride);
                            unsigned char subtype = (&DAT_8047467d)[eStride];
                            (&DAT_805cc198)[DAT_805cc4b4] = nameStr;
                            (&DAT_805cc3f0)[DAT_805cc4b4] = subtype;
                            DAT_805cc4b4 = DAT_805cc4b4 + 1;
                            (&DAT_805cc4fd)[lastCol] = (&DAT_805cc4fd)[lastCol] + 1;
                        }
                        e = e + 1;
                    } while (e < 0x96 && (*(char *)(&DAT_804746ac + eStride) == 0 ? (eStride += 0x30, true) : false));
                }
            }
            grp = grp + 1;
            grpStride = grpStride + 0x30;
            lastCol = col;
        } while (grp < 0x96);
        DAT_805cc4b8 = lastCol + 1;
    }

    // seed team index table (int[4])
    int t = 0, tOff = 0;
    do {
        int teams = mode_team_count();
        if (t < teams)
            *(int *)(&DAT_805cc488 + tOff) = t;
        else
            *(int *)(&DAT_805cc488 + tOff) = -1;
        t += 1;
        tOff += 4;
    } while (t < 4);

    // seed channel index table (int[7])
    int ch = 0, chLeft = 7, chOff = 0;
    do {
        *(int *)(&DAT_805cc498 + chOff) = ch;
        ch += 1;
        chLeft -= 1;
        chOff += 4;
    } while (chLeft != 0);

    int title = get_string_by_id(0x367);
    hud_element_init_from_layout(0x40008000, (void *)(size_t)(unsigned int)title, 0, 0);
    for (int i = 0; i < DAT_80611014; i++) {
        DAT_806119ec = i;
        hud_element_create(frame, (unsigned int)(size_t)&PTR_FUN_80476484, 2);
    }
    hud_element_finalize_frame();
    DAT_806119ec = 0;
    int handlerStr = get_string_by_id(0x19c8);
    hud_screen_register_handler_global(1, handlerStr, 0, 0);
    hud_style_push_element(savedElem);
    hud_style_apply_variant(savedVariant);
    FUN_8038270c();
}

// ===========================================================================
// 0x802d2978  menu_preview_slot_input_tick
// Per-frame per-slot tick driven by the frame-actor at FUN_803826dc (whose
// +0x38 field holds the slot index). Handles: muzzle/effect particle sizing
// from the bot's visual-effects block; reading D-pad + analog into a 4-way
// direction latch; the per-slot UI mode state machine (2=select weapon,
// 4=team select, 6=ready) with weapon cycling and confirm; the two spin
// animations (portrait spin, weapon spin) and their ease timers; and the
// ready-up sound/anim macro triggered on confirm.
// ===========================================================================
void menu_preview_slot_input_tick(void)
{
    int frame = FUN_803826dc();
    int slot = *(int *)(frame + 0x38);
    double animRate = (double)FLOAT_8061a0b4;
    int s16c = slot * 0x16c;
    bool readyEdge = false;
    unsigned int animMacro = 0;
    int *rec = (int *)(&DAT_805cc920 + s16c);

    int dirLatch[4];
    unsigned int btn[4];

    if (hud_element_is_closing(frame) != 0) {
        FUN_80382728();
        return;
    }

    // --- particle sizing from the bot's active visual-effects block ---
    int botPtr = *(int *)(&DAT_805cca74 + slot * 0x5b * 4);
    if (botPtr != 0) {
        int fx = *(int *)(botPtr + 0x7c);
        if (*(short *)(fx + 0x48) == (short)0xa0) {
            character_visual_effects_tick((int *)(fx + 0xc68));
            if ((*(unsigned int *)(fx + 0xc6c) & 4) != 0) {
                float spinB = *(float *)(&DAT_805cca4c + slot * 0x5b * 4);
                double raw = (double)((float)((double)FLOAT_80619ee8 - (double)spinB) *
                                      *(float *)(frame + 0x1b4) *
                                      *(float *)(&DAT_805cca48 + slot * 0x5b * 4));
                double sz = raw;
                if ((double)FLOAT_80619ee8 < raw) sz = (double)FLOAT_80619ee8;
                if ((double)FLOAT_80619f08 <= sz && (double)FLOAT_80619ee8 < raw)
                    sz = (double)FLOAT_80619ee8;
                else if ((double)FLOAT_80619f08 <= sz)
                    sz = raw;

                int macroState = *(int *)(&DAT_805cca5c + slot * 0x5b * 4);
                if (macroState != 5) {
                    if (macroState < 5 && 3 < macroState) {
                        double ph = (double)anim_active_clip_phase(*(int *)(&DAT_805cca74 + slot * 0x5b * 4));
                        sz = (double)(float)(sz * ph);
                    } else {
                        sz = (double)(float)(sz * (double)FLOAT_80619f08);
                    }
                }

                int h;
                h = *(int *)(fx + 0xd24);
                if (particle_handle_is_valid(&h)) {
                    int hh = *(int *)(fx + 0xd24);
                    particle_cached_set_size_range((float)((double)FLOAT_8061a008 * sz),
                                                   (float)((double)FLOAT_8061a008 * sz),
                                                   (ParticleEmitter **)&hh);
                }
                h = *(int *)(fx + 0xd28);
                if (particle_handle_is_valid(&h)) {
                    int hh = *(int *)(fx + 0xd28);
                    particle_cached_set_size_range((float)((double)FLOAT_80619ff4 * sz),
                                                   (float)((double)FLOAT_80619ff4 * sz),
                                                   (ParticleEmitter **)&hh);
                }
                h = *(int *)(fx + 0xd2c);
                if (particle_handle_is_valid(&h)) {
                    int hh = *(int *)(fx + 0xd2c);
                    particle_cached_set_size_range((float)((double)FLOAT_8061a008 * sz),
                                                   (float)((double)FLOAT_8061a008 * sz),
                                                   (ParticleEmitter **)&hh);
                }
                h = *(int *)(fx + 0xd30);
                if (particle_handle_is_valid(&h)) {
                    int hh = *(int *)(fx + 0xd30);
                    particle_cached_set_size_range((float)((double)FLOAT_8061a008 * sz),
                                                   (float)((double)FLOAT_8061a008 * sz),
                                                   (ParticleEmitter **)&hh);
                }
            }
        }
    }

    // --- read direction inputs (buttons then analog fallback) ---
    btn[0] = input_get_buttons_pressed(slot) & 4;
    btn[1] = input_get_buttons_pressed(slot) & 8;
    btn[2] = input_get_buttons_pressed(slot) & 1;
    unsigned int b3 = input_get_buttons_pressed(slot);
    btn[3] = b3 & 2;
    if (btn[0] + btn[1] + btn[2] + (b3 & 2) == 0) {
        double ax = input_get_analog_value(slot, 1);
        double ay = input_get_analog_value(slot, 2);
        if (ABS(ax) <= (double)FLOAT_8061a0b8 || ABS(ax) <= ABS(ay)) {
            if ((double)FLOAT_8061a0b8 < ABS(ay))
                btn[(ay < (double)FLOAT_80619f08) ? 1 : 0] = 1;
        } else {
            btn[(ax >= (double)FLOAT_80619f08) ? 3 : 2] = 1;
        }
    }

    // --- per-direction repeat latch (edge on hold) ---
    int colCount = DAT_805cc4b8;
    {
        int bi = 0, di = 0, n = 4;
        do {
            dirLatch[di / 4] = 0;
            if (*(int *)((char *)btn + di) == 0) {
                *((char *)rec + bi + 0x114) = (char)0xd8;
            } else {
                int f = bi + 0x114;
                *((char *)rec + f) = *((char *)rec + f) + 1;
                if (*((char *)rec + f) < 0x0b) {
                    if (*((char *)rec + f) == -0x27)
                        dirLatch[di / 4] = 1;
                } else {
                    *((char *)rec + f) = 0;
                    dirLatch[di / 4] = 1;
                }
            }
            colCount = DAT_805cc4b8;
            bi += 1;
            di += 4;
            n -= 1;
        } while (n != 0);
    }

    unsigned char uiMode = (&DAT_80612b18)[slot];

    if (uiMode == 4) {
        // team-select mode
        if (dirLatch[1] == 0) {
            if (dirLatch[0] != 0) {
                *(int *)(&DAT_805cca2c + s16c) -= 1;
                if (*(int *)(&DAT_805cca2c + s16c) < 0) {
                    int teams = mode_team_count();
                    *(int *)(&DAT_805cca2c + s16c) = teams - 1;
                }
            }
        } else {
            *(int *)(&DAT_805cca2c + s16c) += 1;
            int teams = mode_team_count();
            if (teams <= *(int *)(&DAT_805cca2c + s16c))
                *(int *)(&DAT_805cca2c + s16c) = 0;
        }
        unsigned int held = input_get_buttons_held(slot);
        if ((held & 0x10) == 0) {
            held = input_get_buttons_held(slot);
            if ((held & 0x80) != 0) {
                int blk = (int)savegame_get_player_block(slot);
                int mi = DAT_806119ec;
                (&DAT_805cc900)[DAT_806119ec] = (unsigned char)*(int *)(&DAT_805cca2c + s16c);
                *(int *)(blk + 0x5c) = (&DAT_805cc900)[mi];
                if (DAT_805cc614 == 0xb || DAT_805cc614 == 0xd ||
                    *(int *)game_get_settings_ptr() == 0) {
                    (&DAT_80612b18)[slot] = (1 < DAT_80611014) ? 6 : 5;
                } else {
                    (&DAT_80612b18)[slot] = 3;
                }
                int i2 = slot * 2;
                *(int *)(&DAT_80612b10 + i2 * 4) = *(unsigned char *)(i2 + -0x7f9ed4ef);
                *(unsigned char *)(i2 + -0x7f9ed4ef) = 4;
            }
        } else {
            (&DAT_80612b18)[slot] = 2;
        }
    } else if (uiMode < 4) {
        if (uiMode == 2) {
            // weapon-select mode
            unsigned int action = 1;
            if (dirLatch[1] == 0) {
                if (dirLatch[0] == 0) {
                    if (dirLatch[3] == 0) {
                        if (dirLatch[2] != 0) {
                            action = 2;
                            rec[*(int *)(&DAT_805cca28 + s16c) + 2] -= 1;
                            if (rec[*(int *)(&DAT_805cca28 + s16c) + 2] < 0) {
                                rec[*(int *)(&DAT_805cca28 + s16c) + 2] =
                                    (&DAT_805cc4fc)[*(int *)(&DAT_805cca28 + s16c)] - 1;
                                unsigned int m = rec[*(int *)(&DAT_805cca28 + s16c) + 2] ^ 0x80000000;
                                *(float *)(&DAT_805cca64 + s16c) =
                                    FLOAT_80619ee8 + (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00);
                            }
                        }
                    } else {
                        action = 3;
                        rec[*(int *)(&DAT_805cca28 + s16c) + 2] += 1;
                        if ((int)(unsigned int)(&DAT_805cc4fc)[*(int *)(&DAT_805cca28 + s16c)] <=
                            rec[*(int *)(&DAT_805cca28 + s16c) + 2]) {
                            rec[*(int *)(&DAT_805cca28 + s16c) + 2] = 0;
                            unsigned int m = rec[*(int *)(&DAT_805cca28 + s16c) + 2] ^ 0x80000000;
                            *(float *)(&DAT_805cca64 + s16c) =
                                (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00) - FLOAT_80619ee8;
                        }
                    }
                } else {
                    action = 0;
                    *(int *)(&DAT_805cca28 + s16c) -= 1;
                    unsigned int cnt = (unsigned int)DAT_80611014;
                    if (*(int *)(&DAT_805cca28 + s16c) < 0) {
                        *(int *)(&DAT_805cca28 + s16c) = DAT_805cc4b8 - 1;
                        unsigned int m = *(unsigned int *)(&DAT_805cca28 + s16c) ^ 0x80000000;
                        *(float *)(&DAT_805cca68 + s16c) =
                            (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00);
                        int cc = ((int)cnt < 3) ? preview_col_count(cnt) : 3;
                        int cclamp = DAT_805cc4b8;
                        if (cc <= DAT_805cc4b8)
                            cclamp = ((int)cnt < 3) ? preview_col_count(cnt) : 3;
                        *(float *)(&DAT_805cca6c + s16c) =
                            (float)((double)CONCAT44(0x43300000, (unsigned int)(DAT_805cc4b8 - cclamp) ^ 0x80000000) -
                                    DOUBLE_80619f00);
                    }
                    unsigned int m = rec[*(int *)(&DAT_805cca28 + s16c) + 2] ^ 0x80000000;
                    *(float *)(&DAT_805cca64 + s16c) =
                        (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00);
                }
            } else {
                action = 1;
                *(int *)(&DAT_805cca28 + s16c) += 1;
                if (colCount <= *(int *)(&DAT_805cca28 + s16c)) {
                    *(int *)(&DAT_805cca28 + s16c) = 0;
                    *(float *)(&DAT_805cca68 + s16c) =
                        (float)((double)CONCAT44(0x43300000, *(unsigned int *)(&DAT_805cca28 + s16c) ^ 0x80000000) -
                                DOUBLE_80619f00);
                    *(float *)(&DAT_805cca6c + s16c) = FLOAT_80619f08;
                }
                unsigned int m = rec[*(int *)(&DAT_805cca28 + s16c) + 2] ^ 0x80000000;
                *(float *)(&DAT_805cca64 + s16c) =
                    (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00);
            }

            if (dirLatch[0] + dirLatch[1] + dirLatch[2] + dirLatch[3] < 1) {
                if (*(int *)(&DAT_805cca38 + s16c) != 0 &&
                    btn[0] + btn[1] + btn[2] + btn[3] == 0 &&
                    menu_preview_slot_matches_selection(slot) == 0) {
                    *(int *)(&DAT_805cca54 + s16c) = 4;
                    *(int *)(&DAT_805cca38 + s16c) = 0;
                }
            } else {
                int wi = weapon_ammo_slot_offset((int)rec);
                menu_preview_slot_set_weapon(slot, (&DAT_805cbf40)[wi],
                                             (int)(char)(&DAT_805cc3f0)[wi], action);
                *(float *)(&DAT_805cca44 + s16c) = FLOAT_80619f5c;
                if (menu_preview_slot_matches_selection(slot) == 0) {
                    if (*(int *)(&DAT_805cca54 + s16c) == 3) {
                        *(int *)(&DAT_805cca54 + s16c) = 4;
                        *(int *)(&DAT_805cca38 + s16c) = 0;
                    } else {
                        *(int *)(&DAT_805cca38 + s16c) = 1;
                    }
                }
            }

            // ease scroll Y toward the selected column
            {
                double db = DOUBLE_80619f00;
                float cur = *(float *)(&DAT_805cca68 + s16c);
                float tgt = (float)((double)CONCAT44(0x43300000, *(unsigned int *)(&DAT_805cca28 + s16c) ^ 0x80000000) - db);
                if (cur <= tgt) {
                    if (cur < tgt) {
                        *(float *)(&DAT_805cca68 + s16c) = cur + FLOAT_8061a0bc;
                        if (tgt < *(float *)(&DAT_805cca68 + s16c))
                            *(float *)(&DAT_805cca68 + s16c) = tgt;
                    }
                } else {
                    *(float *)(&DAT_805cca68 + s16c) = cur - FLOAT_8061a0bc;
                    if (*(float *)(&DAT_805cca68 + s16c) < tgt)
                        *(float *)(&DAT_805cca68 + s16c) = tgt;
                }
            }
            // ease scroll X toward the selected sub-item
            {
                double db = DOUBLE_80619f00;
                float cur = *(float *)(&DAT_805cca64 + s16c);
                float tgt = (float)((double)CONCAT44(0x43300000, (unsigned int)rec[*(int *)(&DAT_805cca28 + s16c) + 2] ^ 0x80000000) - db);
                if (cur <= tgt) {
                    if (cur < tgt) {
                        *(float *)(&DAT_805cca64 + s16c) = cur + FLOAT_8061a0bc;
                        if (tgt < *(float *)(&DAT_805cca64 + s16c))
                            *(float *)(&DAT_805cca64 + s16c) = tgt;
                    }
                } else {
                    *(float *)(&DAT_805cca64 + s16c) = cur - FLOAT_8061a0bc;
                    if (*(float *)(&DAT_805cca64 + s16c) < tgt)
                        *(float *)(&DAT_805cca64 + s16c) = tgt;
                }
            }

            // ease scroll Z (column window)
            {
                int cc = ((int)DAT_80611014 < 3) ? preview_col_count((unsigned int)DAT_80611014) : 3;
                int cclamp = DAT_805cc4b8;
                if (cc <= DAT_805cc4b8)
                    cclamp = ((int)DAT_80611014 < 3) ? preview_col_count((unsigned int)DAT_80611014) : 3;
                int idx = *(int *)(&DAT_805cca28 + s16c);
                unsigned int hi = DAT_805cc4b8 - 1;
                if (idx + 1 < (int)hi) hi = idx + 1;
                hi ^= 0x80000000;
                unsigned int lo = (unsigned int)(cclamp - 1) ^ 0x80000000;
                float tgtHi = (float)((double)CONCAT44(0x43300000, hi) - DOUBLE_80619f00) -
                              (float)((double)CONCAT44(0x43300000, lo) - DOUBLE_80619f00);
                unsigned int loB = (idx - 1 < 0) ? 0 : (unsigned int)(idx - 1);
                loB ^= 0x80000000;
                float tgtLo = (float)((double)CONCAT44(0x43300000, loB) - DOUBLE_80619f00);
                if (tgtLo < *(float *)(&DAT_805cca6c + s16c)) {
                    *(float *)(&DAT_805cca6c + s16c) -= FLOAT_8061a0c0;
                    if (*(float *)(&DAT_805cca6c + s16c) < tgtLo)
                        *(float *)(&DAT_805cca6c + s16c) = tgtLo;
                }
                if (*(float *)(&DAT_805cca6c + s16c) < tgtHi) {
                    *(float *)(&DAT_805cca6c + s16c) += FLOAT_8061a0c0;
                    if (tgtHi < *(float *)(&DAT_805cca6c + s16c))
                        *(float *)(&DAT_805cca6c + s16c) = tgtHi;
                }
            }

            // confirm weapon selection
            if (menu_preview_slot_matches_selection(slot) != 0) {
                unsigned int held = input_get_buttons_held(slot);
                if ((held & 0x80) != 0) {
                    int blk = (int)savegame_get_player_block(slot);
                    if ((DAT_805cc614 == 7 || DAT_805cc614 == 6) ||
                        (DAT_805cc614 == 9 && DAT_805cc68e == 2)) {
                        (&DAT_80612b18)[slot] = (1 < DAT_80611014) ? 6 : 5;
                    } else {
                        *(int *)(blk + 0x60) = *rec;
                        *(int *)(blk + 0x64) = *(int *)(&DAT_805cc924 + s16c);
                        int mi = DAT_806119ec;
                        if (DAT_805cc68e == 0x10) {
                            (&DAT_805cc900)[DAT_806119ec] = 0;
                            *(int *)(blk + 0x5c) = (&DAT_805cc900)[mi];
                        }
                        int feat = mode_feature_enabled_check();
                        mi = DAT_806119ec;
                        if (feat == 0) {
                            if (DAT_805cc614 == 0xb || DAT_805cc614 == 0xd ||
                                *(int *)game_get_settings_ptr() == 0) {
                                (&DAT_80612b18)[slot] = (1 < DAT_80611014) ? 6 : 5;
                            } else {
                                (&DAT_80612b18)[slot] = 3;
                            }
                        } else {
                            (&DAT_80612b18)[slot] = 4;
                            (&DAT_805cc900)[mi] = (unsigned char)0xff;
                        }
                    }
                    int i2 = slot * 2;
                    *(int *)(&DAT_80612b10 + i2 * 4) = *(unsigned char *)(i2 + -0x7f9ed4ef);
                    *(unsigned char *)(i2 + -0x7f9ed4ef) = 2;
                }
            }
        } else if (uiMode < 2) {
            if (uiMode != 0)
                (&DAT_80612b18)[slot] = 2;
        } else {
            // uiMode == 3 : weapon-set cycle
            if (dirLatch[1] == 0) {
                if (dirLatch[0] == 0) {
                    unsigned int held = input_get_buttons_held(slot);
                    if ((held & 0x10) == 0) {
                        held = input_get_buttons_held(slot);
                        if ((held & 0x80) != 0) {
                            int blk = (int)savegame_get_player_block(slot);
                            (&DAT_80612b18)[slot] = (1 < DAT_80611014) ? 6 : 5;
                            int i2 = slot * 2;
                            *(int *)(blk + 0x6c) = *(int *)(&DAT_805cca30 + s16c);
                            *(int *)(&DAT_80612b10 + i2 * 4) = *(unsigned char *)(i2 + -0x7f9ed4ef);
                            *(unsigned char *)(i2 + -0x7f9ed4ef) = 3;
                        }
                    } else {
                        int i2 = slot * 2;
                        unsigned char prev = (&DAT_80612b10)[i2 * 4];
                        (&DAT_80612b18)[slot] = *(unsigned char *)(i2 + -0x7f9ed4ef);
                        *(unsigned char *)(i2 + -0x7f9ed4ef) = prev;
                    }
                } else {
                    *(int *)(&DAT_805cca30 + s16c) -= 1;
                    if (*(int *)(&DAT_805cca30 + s16c) < 0)
                        *(int *)(&DAT_805cca30 + s16c) = 6;
                }
            } else {
                *(int *)(&DAT_805cca30 + s16c) += 1;
                if (6 < *(int *)(&DAT_805cca30 + s16c))
                    *(int *)(&DAT_805cca30 + s16c) = 0;
            }
        }
    } else if (uiMode == 6) {
        unsigned int held = input_get_buttons_held(slot);
        if ((held & 0x10) == 0) {
            held = input_get_buttons_held(slot);
            if ((held & 0x80) != 0)
                readyEdge = true;
        } else {
            int i2 = slot * 2;
            unsigned char prev = (&DAT_80612b10)[i2 * 4];
            (&DAT_80612b18)[slot] = *(unsigned char *)(i2 + -0x7f9ed4ef);
            *(unsigned char *)(i2 + -0x7f9ed4ef) = prev;
        }
    } else if (uiMode < 6) {
        unsigned int held = input_get_buttons_held(slot);
        if ((held & 0x10) == 0) {
            held = input_get_buttons_held(slot);
            if ((held & 0x80) != 0) {
                int i2 = slot * 2;
                unsigned char prev = *(unsigned char *)(i2 + -0x7f9ed4ef);
                (&DAT_80612b18)[slot] = 6;
                readyEdge = true;
                *(int *)(&DAT_80612b10 + i2 * 4) = prev;
                *(unsigned char *)(i2 + -0x7f9ed4ef) = 5;
            }
        } else {
            int i2 = slot * 2;
            unsigned char prev = (&DAT_80612b10)[i2 * 4];
            (&DAT_80612b18)[slot] = *(unsigned char *)(i2 + -0x7f9ed4ef);
            *(unsigned char *)(i2 + -0x7f9ed4ef) = prev;
        }
    }

    // --- anim macro selection when this slot is the confirmed selection ---
    if (menu_preview_slot_matches_selection(slot) != 0) {
        unsigned char *wrec = weapon_def_find_record(*rec, *(int *)(&DAT_805cc924 + s16c));
        int macroState = *(int *)(&DAT_805cca5c + slot * 0x5b * 4);
        if (macroState == 3) {
            *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 4;
            animMacro = 2;
            animRate = ((*(unsigned int *)(&DAT_80473d68 + *(int *)(wrec + 0x2c) * 0x14) & 1) == 0)
                           ? (double)FLOAT_8061a0b4 : (double)FLOAT_80619f08;
        } else if (macroState < 3) {
            if (macroState == 1) {
                unsigned int held = input_get_buttons_held(slot);
                if ((held & 0x80) != 0) {
                    if ((*(unsigned int *)(&DAT_80473d68 + *(int *)(wrec + 0x2c) * 0x14) & 1) == 0)
                        *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 3;
                    else
                        *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 2;
                }
            } else if (macroState > 0) {
                int uf = anim_active_slot_userflags(*(int *)(*(int *)(&DAT_805cca74 + slot * 0x5b * 4) + 4));
                if (uf != 0)
                    *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 3;
            }
        } else if (macroState == 5) {
            if ((&DAT_80612b18)[slot] == 2) {
                *(int *)(&DAT_805cca50 + s16c) = 2;
                *(int *)(&DAT_805cca58 + s16c) = 1;
            }
        } else if (macroState < 5) {
            if ((&DAT_80612b18)[slot] == 2) {
                *(int *)(&DAT_805cca50 + s16c) = 2;
                *(int *)(&DAT_805cca58 + s16c) = 1;
            } else {
                int fin = anim_secondary_clip_finished(*(int *)(*(int *)(&DAT_805cca74 + slot * 0x5b * 4) + 4));
                if (fin == 0) {
                    if (readyEdge) {
                        *(int *)(&DAT_805cca50 + s16c) = 2;
                        *(int *)(&DAT_805cca58 + s16c) = 3;
                        sound_register_fade_out((double)FLOAT_80619f88, (double)FLOAT_80619f4c,
                                                (int *)(*(int *)(&DAT_805cca74 + slot * 0x5b * 4) + 0x22c),
                                                (unsigned int *)0);
                    }
                } else {
                    animRate = (double)FLOAT_8061a0b4;
                    *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 5;
                    animMacro = 3;
                }
            }
        }
    }

    // --- portrait spin (state A) ease ---
    {
        int stA = *(int *)(&DAT_805cca50 + s16c);
        if (stA != 3) {
            if (stA < 3) {
                if (1 < stA) {
                    float *spinA = (float *)(&DAT_805cca48 + slot * 0x5b * 4);
                    if (*spinA <= FLOAT_80619ee8) {
                        unsigned int m = DAT_806109c0 ^ 0x80000000;
                        *spinA = FLOAT_80619f84 * FLOAT_806108e8 *
                                 (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) + *spinA;
                    }
                    if (FLOAT_80619ee8 < *spinA) {
                        *spinA = FLOAT_80619ee8;
                        *(int *)(&DAT_805cca50 + s16c) = 4;
                        int sub = *(int *)(&DAT_805cca58 + s16c);
                        if (sub != 2) {
                            if (sub < 2) {
                                if (0 < sub) {
                                    animRate = (double)FLOAT_80619f08;
                                    *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 1;
                                    animMacro = 1;
                                }
                            } else if (sub < 4) {
                                animRate = (double)FLOAT_80619f08;
                                *(int *)(&DAT_805cca5c + slot * 0x5b * 4) = 5;
                                animMacro = 3;
                            }
                        }
                        *(int *)(&DAT_805cca58 + s16c) = 0;
                    }
                }
            } else if (stA < 5) {
                float *spinA = (float *)(&DAT_805cca48 + slot * 0x5b * 4);
                if (FLOAT_80619f08 <= *spinA) {
                    unsigned int m = DAT_806109c0 ^ 0x80000000;
                    *spinA = -(FLOAT_80619f84 * FLOAT_806108e8 *
                               (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) - *spinA);
                }
                if (*spinA < FLOAT_80619f08) {
                    *spinA = FLOAT_80619f08;
                    *(int *)(&DAT_805cca50 + s16c) = 1;
                }
            }
        }
    }

    // --- weapon spin (state B) ease ---
    {
        int stB = *(int *)(&DAT_805cca54 + s16c);
        float *spinB = (float *)(&DAT_805cca4c + slot * 0x5b * 4);
        if (stB == 2) {
            if (*spinB <= FLOAT_80619ee8) {
                unsigned int m = DAT_806109c0 ^ 0x80000000;
                *spinB = FLOAT_80619f84 * FLOAT_806108e8 *
                         (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) + *spinB;
            }
            if (FLOAT_80619ee8 < *spinB) {
                *spinB = FLOAT_80619ee8;
                *(int *)(&DAT_805cca54 + s16c) = 3;
            }
        } else if (stB < 2) {
            if (stB == 0 && *(int *)(frame + 0x1ac) == 3) {
                *spinB = FLOAT_80619f08;
                *(int *)(&DAT_805cca54 + s16c) = 1;
            }
        } else if (stB == 4) {
            if (FLOAT_80619f08 <= *spinB) {
                unsigned int m = DAT_806109c0 ^ 0x80000000;
                *spinB = -(FLOAT_80619f84 * FLOAT_806108e8 *
                           (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) - *spinB);
            }
            if (*spinB < FLOAT_80619f08) {
                *spinB = FLOAT_80619f08;
                *(int *)(&DAT_805cca54 + s16c) = 1;
            }
        }
    }

    // --- team-cursor open animation (state, offset 0x40) ---
    if ((&DAT_80612b18)[slot] == 2) {
        unsigned int held = input_get_buttons_held(slot);
        if ((held & 0x20) != 0) {
            int st = *(int *)(&DAT_805cca40 + s16c);
            if (st == 1 || st == 4)
                *(int *)(&DAT_805cca40 + s16c) = 2;
            else if (st == 3 || st == 2)
                *(int *)(&DAT_805cca40 + s16c) = 4;
        }
    }
    {
        int st = *(int *)(&DAT_805cca40 + s16c);
        float *cur = (float *)(&DAT_805cca3c + s16c);
        if (st != 3) {
            if (st < 3) {
                if (1 < st) {
                    if (*cur <= FLOAT_80619ee8) {
                        unsigned int m = DAT_806109c0 ^ 0x80000000;
                        *cur = FLOAT_80619f84 * FLOAT_806108e8 *
                               (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) + *cur;
                    }
                    if (FLOAT_80619ee8 < *cur) {
                        *cur = FLOAT_80619ee8;
                        *(int *)(&DAT_805cca40 + s16c) = 3;
                    }
                }
            } else if (st < 5) {
                if (FLOAT_80619f08 <= *cur) {
                    unsigned int m = DAT_806109c0 ^ 0x80000000;
                    *cur = -(FLOAT_80619f84 * FLOAT_806108e8 *
                             (FLOAT_80619ee8 / (float)((double)CONCAT44(0x43300000, m) - DOUBLE_80619f00)) - *cur);
                }
                if (*cur < FLOAT_80619f08) {
                    *cur = FLOAT_80619f08;
                    *(int *)(&DAT_805cca40 + s16c) = 1;
                }
            }
        }
    }

    // --- fire the selected anim macro (play + sound) ---
    if (menu_preview_slot_matches_selection(slot) != 0) {
        unsigned char *wrec = weapon_def_find_record(*rec, *(int *)(&DAT_805cc924 + s16c));
        if (animMacro != 0) {
            int animTbl = *(int *)(wrec + 0x2c);
            int slotPtr = *(int *)(&DAT_805cca74 + slot * 0x5b * 4);
            if (animMacro == 2) {
                anim_play_scaled(animRate, (double)FLOAT_80619ee8);
                sound_play_or_resume_3d((double)FLOAT_80619f08, (double)FLOAT_80619ee8,
                                        (double)FLOAT_80619f88,
                                        *(int *)(&DAT_805cca88 + slot * 0x5b * 4), (float *)0, 1);
            } else if (animMacro < 2) {
                sound_register_fade_out((double)FLOAT_80619f88, (double)FLOAT_80619f4c,
                                        (int *)(slotPtr + 0x22c), (unsigned int *)0);
                anim_play_scaled(animRate, (double)FLOAT_80619ee8);
                sound_play_persistent(*(int *)(&DAT_805cca88 + slot * 0x5b * 4), 0, 1);
            } else if (animMacro < 4) {
                anim_play_scaled(animRate, (double)FLOAT_80619ee8);
            }
            if (*(int *)(&DAT_80473d64 + animTbl * 0x14) != -1) {
                if (animMacro == 2)
                    anim_play_scaled(animRate, (double)FLOAT_80619ee8);
                else if (animMacro < 2)
                    anim_play_scaled(animRate, (double)FLOAT_80619ee8);
                else if (animMacro < 4)
                    anim_play_scaled(animRate, (double)FLOAT_80619ee8);
            }
        }
    }

    menu_preview_slot_stream_tick();
    FUN_80382728();
}
