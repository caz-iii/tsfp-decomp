// arcade_edit_options.cpp
// Arcade map-editor "options" screens and the test-match launcher.
//
// Covers the weapon-set confirm sub-screen builder + tick, the per-option
// options list builder + its reset/confirm dialogs, the weapon-slot collector
// used when spawning bots, and the big "start test/play match" routine that
// assembles the match-setup block (player/bot slots, teams, handicap) from the
// current editor selection (game-mode index DAT_805d59a4, mode row table
// DAT_80480000 stride 0x24, level-def table DAT_803f2dd0/DAT_803f2de4 stride
// 0x20) and hands it to match_start_cutscene_transition.
//
// Editor context DAT_80612be4 fields used here: +0x200 weapon-set, +0x22c toggle,
// +0x230/+0x234 value displays, +0x25c screen state bits, +0x41e0 placed-object
// count with the 0x220-stride record array at +0x41e4.
//
// Address: 0x8031f0b4..0x8031fe9c | Batch: batch_901 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int  DAT_80612be4;      // editor screen context base
extern int  DAT_805d59a0;      // game-mode / weapon-set selector base
extern int  DAT_805d59a4;      // weapon-set / mode-row selector
extern int  DAT_805d59b4;      // per-slot spawn-template block
extern int  DAT_805d59d4;      // random team seed A
extern int  DAT_805d59d8;      // random team seed B
extern int  DAT_806111b0;      // weapon-set baseline / linked value
extern int  DAT_80612ca1;      // active option row index
extern int  DAT_80612ca4;      // slot-count adjust
extern int  DAT_8060f948;      // options-menu layout param index
extern int  DAT_805d63d4;      // dialog message buffer
extern int  DAT_805d6358;      // per-team value display block base
extern int  DAT_806119e0;      // layout metric (pos-y arg)

extern unsigned char DAT_80501570;   // default slot count
extern int  DAT_805015c8;             // default flag value
extern unsigned char DAT_80480000;    // mode row table (stride 0x24): +0 label, +4 warn, +8/+c tags, +0x10 flags, +0x14 mask
extern unsigned char DAT_80480004;
extern unsigned char DAT_80480008;
extern unsigned char DAT_8048000c;
extern unsigned char DAT_80480010;
extern unsigned char DAT_80480014;
extern int  DAT_8047fff8;             // index-into-leveldef table (stride 0x24; also [i*9])
extern int  DAT_803f2dd0;             // level-def flags block base (stride 0x20)
extern int  DAT_803f2dd4;             // level-def mask block base  (stride 0x20)
extern short DAT_803f2de4;            // level-def name string-id array (stride 0x20)

extern float FLOAT_8061a830;
extern float FLOAT_8061a834;
extern float FLOAT_8061a838;
extern float FLOAT_8061a83c;
extern float FLOAT_8061a844;
extern float FLOAT_8061a84c;
extern float FLOAT_8061a850;
extern float FLOAT_8061a854;
extern float FLOAT_8061a858;
extern float FLOAT_80611a5c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;

// ---- helpers defined elsewhere (raw, per-file externs) ----
extern int  __save_gpr_25(void);
extern void __restore_gpr_25(void);
extern int  __save_gpr_28(void);
extern void __restore_gpr_28(void);
extern int  get_string_by_id(int stringId);
extern void memset_wrapper(int dst, int value, int len);
extern int  heap_arena_lookup(void);
extern unsigned int rng_counter_tick(void);
extern int  weapon_type_is_projectile(int weaponType);
extern int  menu_slot_tier_scale(int weaponType);
extern int *menu_slot_option_table_resolve(int set);
extern int  player_ai_is_story_mode(unsigned int slotType);
extern void aveditor_serialize_map(void);
// avanim_play_move_sound is void; the decompiler's captured "return" is a leftover
// register, so we call it for effect and pass a1 as the format ctx.
extern void avanim_play_move_sound(int soundId);
extern void sound_set_mute_flag(unsigned int mute);
extern int  hud_element_has_unsaved_changes(int screen);

extern int  aveditor_tally_slot(int row);
extern unsigned int aveditor_tally_and_trigger(void);
extern double aveditor_screen_param_y(int index);

// HUD list-builder primitives
extern void hud_element_init_from_layout(int layoutId, int a, int b, void *cb);
extern void hud_set_item_padding_mode(double h, int a, int b, int c);
extern void hud_set_context_property(int flags);
extern void hud_element_scroll_h_overflow(double x, double a, double b, int c);
extern void hud_begin_scroll_region(void);
extern void hud_begin_scroll_with_item(void);
extern void hud_begin_sub_list(int a, int b, int c);
extern void hud_end_sub_list(void);
extern double hud_get_content_height(void);
extern void hud_add_item_internal(double a, double b);
extern void hud_add_list_item(void);
extern void hud_item_add_header(double h, unsigned int locked);
extern void hud_item_add_footer(double h, int a);
extern void hud_item_add_text_display(double h, int a);
extern void hud_item_add_labeled_pair(int a, int b, int c, int d);
extern void hud_event_add_string(int stringId);
extern void hud_set_last_child_tag(int tag);
extern void hud_item_set_value_display(int valuePtr);
extern void hud_item_set_toggle(int backing, int item);
extern int  hud_get_last_created_item(void);
extern int  hud_context_is_active(void);
extern void hud_end_scroll_region(void);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);
extern void hud_element_set_pos_y(double y, int metric);
extern void hud_create_yes_no_dialog(int screen, int msg, int yesCb, int noCb);
extern void hud_create_back_dialog(int screen, int *msg, int arg);

// sub-screen exit handler defined in arcade_edit_settings.cpp (batch_900)
extern void aveditor_weaponset_exit_handler(int screen);

// draw helpers
extern void color_scale_lo(double a, int color);
extern void sprite_draw_bordered_frame(int x0, int y0, int x1, int y1, int a);

// text/format helpers
extern int  FUN_8038b510(int *dst, int *src);
extern void FUN_80387fb8(unsigned long long ctx, unsigned long long a2, unsigned long long a3,
                         unsigned long long a4, unsigned long long a5, unsigned long long a6,
                         unsigned long long a7, unsigned long long a8, int titleCb, int fmt,
                         int s1, int s2, int r7, int r8, int r9, int r10);

// register save/restore stubs (no-ops on PC); FUN_803826dc returns the screen ctx
extern int  FUN_803826dc(void);
extern void FUN_80382728(void);

// placed-object list scan (returns matching record or 0)
extern int  FUN_8032cfcc(void);

// match launcher
extern void match_start_cutscene_transition(unsigned long long a1, unsigned long long a2,
                                            unsigned long long a3, unsigned long long a4,
                                            unsigned long long a5, unsigned long long a6,
                                            unsigned long long a7, unsigned long long a8,
                                            unsigned int setupPtr, unsigned int a10,
                                            unsigned char *a11, unsigned char *a12,
                                            unsigned char *r7, int r8, int r9, int r10);

// 0x8031f0b4: build the weapon-set confirm sub-screen (layout 0xa800) - title,
// two action rows (accept/decline tagged 0x3a/0x3b) bound to the weapon-set value,
// a toggle row (0x3c) for +0x22c, and two value displays for +0x230/+0x234.
void aveditor_build_weaponset_screen(int screen)
{
    int stringId;
    int item;

    hud_element_init_from_layout(0xa800, 0, 0, (void *)aveditor_weaponset_exit_handler);
    stringId = get_string_by_id(0xa80);
    *(int *)(screen + 0x200) = stringId;
    *(int *)(screen + 0x214) = 5;
    hud_set_item_padding_mode((double)FLOAT_8061a84c, 1, 1, 1);
    hud_set_context_property(0x20000);

    get_string_by_id(0xaa6);
    hud_begin_scroll_region();
    get_string_by_id(0x19b4);
    hud_add_list_item();
    get_string_by_id(0xa85);
    hud_add_list_item();
    hud_add_list_item();
    hud_item_add_footer((double)FLOAT_8061a834, 0);
    stringId = get_string_by_id(0xaa4);
    hud_event_add_string(stringId);
    hud_set_last_child_tag(0x3a);
    stringId = get_string_by_id(0xaa5);
    hud_event_add_string(stringId);
    hud_set_last_child_tag(0x3b);
    if (hud_context_is_active() != 0)
    {
        DAT_806111b0 = *(int *)(DAT_80612be4 + 0x200);
    }
    hud_item_set_value_display((int)&DAT_806111b0);
    hud_end_scroll_region();

    get_string_by_id(0xa5a);
    hud_begin_scroll_with_item();
    hud_item_add_labeled_pair(0, 0, 0, 0);
    hud_set_last_child_tag(0x3c);
    item = hud_get_last_created_item();
    hud_item_set_toggle(DAT_80612be4 + 0x22c, item);
    hud_end_scroll_region();

    get_string_by_id(0xa5b);
    hud_begin_scroll_with_item();
    get_string_by_id(0xa60);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a834, 0);
    hud_item_set_value_display(DAT_80612be4 + 0x230);
    get_string_by_id(0xa5c);
    hud_add_list_item();
    hud_item_add_text_display((double)FLOAT_8061a834, 0);
    hud_item_set_value_display(DAT_80612be4 + 0x234);
    hud_end_scroll_region();

    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a830 * FLOAT_80611a64 +
                   FLOAT_8061a838 * FLOAT_80611a64 +
                   FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    return;
}

// 0x8031f31c: weapon-set sub-screen tick - when the screen has unsaved edits, drive
// the audio mute flag from whether the +0x230 value display is zero.
void aveditor_weaponset_tick(int screen)
{
    unsigned int isZero;

    if (hud_element_has_unsaved_changes(screen) != 0)
    {
        isZero = countLeadingZeros(*(unsigned int *)(DAT_80612be4 + 0x230));
        sound_set_mute_flag(isZero >> 5);
    }
    return;
}

// 0x8031f35c: collect up to 10 weapon-slot entries from the resolved option table
// into three parallel byte arrays: weaponId (outWeapon+i+1), projectile-ammo
// (outAmmo+i+1 when the weapon is a projectile), and tier scale (outTier+i+1).
// Walks the 10 option rows at (optionTable + i*8 + 4), skipping empty (-1) rows and
// stopping at 'limit' accepted entries. Returns the count accepted.
int aveditor_collect_weapon_slots(int optionTable, int limit, int outWeapon, int outAmmo,
                                  int outTier)
{
    short *entry;
    int count;
    int i;
    int off;

    count = 0;
    i = 0;
    off = 0;
    do
    {
        entry = (short *)(optionTable + off + 4);
        if ((*entry != -1) && (count < limit))
        {
            *(char *)(outWeapon + count + 1) = (char)*entry;
            if (weapon_type_is_projectile((int)*entry) != 0)
            {
                *(char *)(outAmmo + count + 1) = (char)entry[3];
            }
            *(char *)(outTier + count + 1) = (char)menu_slot_tier_scale((int)*entry);
            count = count + 1;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 10);
    return count;
}

// 0x8031f410: start a test/play match from the current editor selection. Builds the
// setup block on the stack (slot counts, per-slot weapon/team/handicap layout keyed
// off the game-mode index) and hands it to the match launcher. Faithful non-matching
// translation of the original stack layout.
void aveditor_start_test_match(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8)
{
    unsigned char setup[0x98];       // local_b8 block (0x98 bytes)
    unsigned char *slotWeapon;       // local_a4
    unsigned char *slotTeam;         // auStack_88
    unsigned char *slotHandicap;     // abStack_7a
    unsigned char *slotTier;         // local_96
    int *optionTbl;
    int modeRowOff;
    unsigned int modeId;
    unsigned int flags;
    int i;
    int j;
    int storyCount;
    unsigned int r10 = 0, r9 = 0;
    int r8 = 0;
    unsigned char *r7 = 0;
    unsigned int a10 = 0;
    unsigned char *a11 = 0;
    unsigned char *a12 = 0;

    __save_gpr_25();

    modeRowOff = (int)DAT_80612ca1 * 0x24;
    modeId = *(unsigned int *)((int)&DAT_80480000 + modeRowOff);   // mode-row id field

    optionTbl = menu_slot_option_table_resolve(DAT_805d59a4);
    memset_wrapper((int)setup, 0, 0x98);

    slotWeapon   = setup + 0x1c;     // local_a4 within block
    slotTier     = setup + 0x2a;     // local_96
    slotTeam     = setup + 0x30;     // auStack_88
    slotHandicap = setup + 0x3e;     // abStack_7a
    memset_wrapper((int)slotHandicap, 0xff, 0xe);

    *(unsigned int *)(DAT_80612be4 + 0x25c) = *(unsigned int *)(DAT_80612be4 + 0x25c) | 0x200;
    aveditor_serialize_map();

    flags = 0;
    setup[0] = DAT_80501570;
    if ((*(unsigned int *)((int)&DAT_80480010 + modeRowOff) & 2) == 0)
    {
        setup[0] = (unsigned char)((char)DAT_80612ca4 + 1);
    }
    setup[2] = 0;
    slotWeapon[0] = 0xc;

    if ((heap_arena_lookup() == 0) && (modeId != 2) && (modeId != 0))
    {
        setup[2] = (unsigned char)*optionTbl;
        if ((char)setup[2] > 7)
        {
            setup[2] = 7;
        }
        setup[2] = (unsigned char)aveditor_collect_weapon_slots(
            (int)optionTbl, (int)(char)setup[2], (int)slotWeapon, (int)slotHandicap, (int)slotTeam);

        for (unsigned int k = 0; (int)k < (int)(char)setup[2]; k = k + 1)
        {
            if ((modeId == 5) || (modeId == 0xf) || (modeId == 4))
            {
                slotTier[k] = (unsigned char)(((k & 1) != 0) ? 2 : 1);
            }
            else if (modeId == 0x10)
            {
                unsigned char v = (unsigned char)(((k & 1) != 0) ? 1 : 2);
                setup[k + (int)(char)setup[0] + 0x22] = v;
            }
            else if (modeId == 0x11)
            {
                unsigned int clz = countLeadingZeros((k & 1));
                setup[k + (int)(char)setup[0] + 0x22] = (unsigned char)((clz >> 5) + 1);
            }
            else
            {
                slotTier[k] = 0;
            }
        }
    }

    setup[0x14] = 100;                        // local_6c
    setup[0x16] = (unsigned char)modeId;      // local_6a
    setup[0x15] = (unsigned char)*(int *)((int)&DAT_803f2dd0 + modeId * 0x20);  // local_6b
    setup[0x17] = 1;                          // local_69

    if (modeId == 2)
    {
        flags = flags | 0x4080008;
    }
    else
    {
        flags = flags | 0x40d0000;
        if ((modeId & 0xff) != 0)
        {
            flags = 0x40d8402;
        }
        *(int *)(setup + 0x58) = DAT_805015c8;                 // local_60
        if ((*(unsigned int *)((int)&DAT_80480010 + modeRowOff) & 1) == 0)
        {
            *(int *)(setup + 0x58) = 0;
        }
        *(int *)(setup + 0x5c) = 0;                            // local_5c
    }
    *(unsigned int *)(setup + 0x50) = flags;                   // local_68

    switch (modeId & 0xff)
    {
    case 4:
    case 0xf:
    case 5:
        setup[3] = 2;
        setup[4] = 1;
        setup[5] = 2;
        slotTier[0] = 1;
        flags = flags | 1;
        break;
    case 7:
        *(int *)(setup + 0x5c) = 0;
        *(int *)(setup + 0x6c) = 10;                           // local_4c
        break;
    case 9:
        *(int *)(setup + 0x6c) = 10;
        *(float *)(setup + 0x84) = FLOAT_8061a850;             // local_34
        *(int *)(setup + 0x5c) = 0;
        flags = (flags & 0xffffffef) | 0x20;
        break;
    case 0xc:
        setup[0x15] = 5;
        setup[0x16] = 0xc;
        break;
    case 0x10:
    {
        setup[5] = 2;
        setup[4] = 1;
        setup[3] = 2;
        flags = flags | 1;
        DAT_805d59d4 = (int)(rng_counter_tick() % 6);
        DAT_805d59d8 = (int)(rng_counter_tick() % 6);
        j = 0;
        for (i = 0; i < (char)setup[0]; i = i + 1)
        {
            int v = *(int *)((int)&DAT_805d59b4 + j);
            j = j + 4;
            setup[i + 0x2a] = (unsigned char)((char)v + 1);
        }
        {
            int n = (int)setup[3] * 4;
            int cnt = -(int)setup[3] * 4 + 8;
            int p = n;
            if (n < 8)
            {
                do
                {
                    setup[p + 0x2a] = 0;
                    p = p + 1;
                    cnt = cnt - 1;
                } while (cnt != 0);
            }
        }
        break;
    }
    case 0x11:
        flags = flags | 0x401;
        setup[2] = (unsigned char)(8 - setup[0]);
        setup[4] = 2;
        setup[5] = 1;
        setup[3] = 2;
        for (i = 0; i < (int)(char)setup[0]; i = i + 1)
        {
            setup[i + 0x2a] = 1;
        }
        break;
    case 0x13:
        storyCount = 0;
        for (i = 0; i < (int)(char)setup[2]; i = i + 1)
        {
            if (player_ai_is_story_mode((unsigned int)setup[i + (char)setup[0] + 0x1c]) != 0)
            {
                storyCount = storyCount + 1;
            }
        }
        if (storyCount < 3)
        {
            int need = 3 - storyCount;
            int base = (int)(char)setup[0] + (int)(char)setup[2];
            for (i = 0; i < need; i = i + 1)
            {
                setup[base + i + 0x14] = 0x5d;
                setup[base + i + 0x3e] = 0xff;
                setup[2] = setup[2] + 1;
            }
        }
        break;
    }

    match_start_cutscene_transition(a1, a2, a3, a4, a5, a6, a7, a8, (unsigned int)(int)setup,
                                    a10, a11, a12, r7, r8, r9, r10);
    __restore_gpr_25();
    return;
}

// 0x8031f9a8: draw a bordered selection frame inset from the rect by a fixed
// fraction, tinted by the element alpha at obj+0xf0.
void aveditor_draw_selection_frame(int obj, int a, float *rect)
{
    double dx;
    double dy;
    double insetX;
    double insetY;
    double x;
    double y;

    (void)a;
    dx = (double)(rect[2] - rect[0]);
    dy = (double)(rect[3] - rect[1]);
    insetX = (double)(float)((double)FLOAT_8061a854 * dx);
    insetY = (double)(float)((double)FLOAT_8061a854 * dy);
    x = (double)(FLOAT_8061a84c * (float)(dx - insetX) + rect[0]);
    y = (double)(FLOAT_8061a84c * (float)(dy - insetY) + rect[1]);
    color_scale_lo((double)*(float *)(obj + 0xf0), 0x3c50643f);
    sprite_draw_bordered_frame((int)x, (int)y, (int)(x + insetX), (int)(y + insetY), 0);
    return;
}

// 0x8031fa94: build the option confirm/warning back dialog for the selected option
// row (DAT_80480000 table). Formats either a plain warning or a "%s: %s" message
// depending on the row flags, then shows a back dialog.
void aveditor_option_confirm_dialog(unsigned long long a1, unsigned long long a2,
                                    unsigned long long a3, unsigned long long a4,
                                    unsigned long long a5, unsigned long long a6,
                                    unsigned long long a7, unsigned long long a8)
{
    int screen;
    int row;
    int rowOff;
    int tally;
    int nameId;
    int *msgSrc;
    char *fmt;
    int arg2;

    screen = __save_gpr_28();
    row = *(int *)(screen + 0x188);
    rowOff = row * 0x24;
    if (*(int *)((int)&DAT_80480000 + rowOff) != -1)
    {
        tally = aveditor_tally_slot(row);
        arg2 = (&DAT_8047fff8)[row * 9] * 0x20;
        memset_wrapper(-0x7fa29c2c, 0, 0x80);
        avanim_play_move_sound(0x1575);
        if (tally == 0)
        {
            if ((*(unsigned int *)((int)&DAT_80480010 + rowOff) & 4) == 0)
            {
                msgSrc = (int *)get_string_by_id(*(int *)((int)&DAT_80480000 + rowOff));
                FUN_8038b510((int *)&DAT_805d63d4, msgSrc);
            }
            else
            {
                nameId = get_string_by_id((int)*(short *)((int)&DAT_803f2de4 + arg2));
                fmt = (char *)get_string_by_id(*(int *)((int)&DAT_80480000 + rowOff));
                FUN_80387fb8(a1, a2, a3, a4, a5, a6, a7, a8, -0x7fa29c2c, (int)fmt, nameId, arg2,
                             0, 0, 0, 0);
            }
        }
        else
        {
            int titleId = get_string_by_id(0xa7a);
            nameId = get_string_by_id((int)*(short *)((int)&DAT_803f2de4 + arg2));
            fmt = (char *)get_string_by_id(0xa93);
            FUN_80387fb8(a1, a2, a3, a4, a5, a6, a7, a8, -0x7fa29c2c, (int)fmt, nameId, titleId,
                         0, 0, 0, 0);
        }
        hud_create_back_dialog(screen, &DAT_805d63d4, 0);
    }
    __restore_gpr_28();
    return;
}

// 0x8031fbcc: build the editor options list. Walks the 15-row mode table
// (DAT_8047fff8 stride 0x24) and, for rows enabled for the current weapon-set
// (+0x200 bit), emits either a two-item sub-list (label + value) or a single list
// item, tagged from the per-row tag fields.
void aveditor_build_options_menu(void)
{
    int screen;
    unsigned int trigger;
    int row;
    int rowOff;
    int valOff;
    int defOff;
    unsigned int mask;
    double h;

    screen = FUN_803826dc();          // returns the screen ctx
    trigger = aveditor_tally_and_trigger();
    aveditor_screen_param_y(DAT_8060f948);
    hud_element_init_from_layout(0xa800, 0, 0, 0);
    *(int *)(screen + 0x214) = 1;
    hud_set_item_padding_mode((double)FLOAT_8061a858, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061a830 * FLOAT_80611a64 +
                    FLOAT_8061a838 * FLOAT_80611a64 +
                    FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061a844),
        (double)FLOAT_8061a834, (double)FLOAT_8061a834, 0);

    row = 0;
    valOff = 0;
    rowOff = 0;
    do
    {
        defOff = *(int *)((int)&DAT_8047fff8 + rowOff) * 0x20;
        mask = trigger & *(unsigned int *)((int)&DAT_803f2dd4 + defOff);
        if (((unsigned int)(unsigned char)(&DAT_80480014)[rowOff] &
             (1 << *(int *)(DAT_80612be4 + 0x200))) != 0)
        {
            if (*(int *)((int)&DAT_805d6358 + valOff) != 0)
            {
                mask = 0;
            }
            if (mask == 0)
            {
                hud_begin_sub_list(0, 0, 2);
                h = hud_get_content_height();
                h = (double)(float)(h - (double)FLOAT_8061a83c);
                get_string_by_id((int)*(short *)((int)&DAT_803f2de4 + defOff));
                hud_add_item_internal(h, (double)FLOAT_8061a834);
                hud_set_last_child_tag(*(int *)((int)&DAT_80480008 + rowOff));
                get_string_by_id(0xa87);
                hud_add_item_internal((double)FLOAT_8061a834, (double)FLOAT_8061a834);
                hud_set_last_child_tag(*(int *)((int)&DAT_8048000c + rowOff));
                hud_end_sub_list();
            }
            else
            {
                get_string_by_id((int)*(short *)((int)&DAT_803f2de4 + defOff));
                hud_add_list_item();
                hud_set_last_child_tag(*(int *)((int)&DAT_80480008 + rowOff));
            }
        }
        row = row + 1;
        rowOff = rowOff + 0x24;
        valOff = valOff + 4;
    } while (row < 0xf);

    hud_element_apply_scroll();
    hud_element_finalize_frame();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a830 * FLOAT_80611a64 +
                   FLOAT_8061a838 * FLOAT_80611a64 +
                   FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    FUN_80382728();
    return;
}

// 0x8031fe0c: option reset prompt - play the move sound and raise a yes/no dialog
// using the "reset" message for the active option row.
void aveditor_option_reset_prompt(int screen)
{
    int msg;

    avanim_play_move_sound(0x1575);
    msg = get_string_by_id(*(int *)((int)&DAT_80480004 + (unsigned int)DAT_80612ca1 * 0x24));
    hud_create_yes_no_dialog(screen, msg, -0x7fce0bf0, 0);
    return;
}

// 0x8031fe70: predicate - true when a matching placed object exists in the editor.
bool aveditor_has_placed_object(void)
{
    return FUN_8032cfcc() != 0;
}

// 0x8031fe9c: predicate - true when a matching placed object exists (duplicate of
// aveditor_has_placed_object at a separate entry point).
bool aveditor_has_placed_object_2(void)
{
    return FUN_8032cfcc() != 0;
}
