// arcade_edit_weaponcfg.cpp
// Arcade map-editor weapon/bot config screens, option dispatch, and the
// weapon-config (weapon-slot / bot-count) serialization helpers.
//
// The weapon config lives in the level command stream (tag 0xc77668, found via
// FUN_80339b90) or, when editing (DAT_80612614 != 0x66), in the editor context
// block at DAT_80612be4+0x11860. aveditor_resolve_weapon_config returns whichever
// applies; the store/load helpers marshal weapon ids, ammo and tier bytes between
// the working slot table and that block, and the *_config_changed predicates
// dirty-check the working config against the saved baseline.
//
// Address: 0x8031fec8..0x80320424 | Batch: batch_902 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int  DAT_80612be4;      // editor screen context base
extern int  DAT_80612be8;      // active level command stream base
extern int  DAT_80612614;      // editor-state discriminator (0x66 => in-editor)
extern int  DAT_80612ca1;      // active option row index
extern int  DAT_805d59b4;      // per-slot spawn-template block / linked value
extern int  DAT_806109c0;      // time-limit base (minutes)
extern int  DAT_805d6454;      // resolved weapon-config: count
extern int  DAT_805d6458;      // resolved weapon-config: entry[0].weaponId
extern int  DAT_805d645e;      // resolved weapon-config: entry[0].tier
extern int  DAT_806119e0;      // layout metric (pos-y arg)

extern unsigned char DAT_80501570;    // default slot-count block base
extern unsigned char DAT_8047fffc;    // option-row callback table (stride 0x24)
extern unsigned char DAT_80480018;    // option-row "has sub-screen" flag (stride 0x24)

extern float FLOAT_8061a830;
extern float FLOAT_8061a834;
extern float FLOAT_8061a838;
extern float FLOAT_8061a83c;
extern float FLOAT_8061a844;
extern float FLOAT_8061a860;
extern float FLOAT_80611a5c;
extern float FLOAT_80611a64;
extern float FLOAT_80611a6c;

// ---- helpers defined elsewhere (raw, per-file externs) ----
extern int  get_string_by_id(int stringId);
extern void memset_wrapper(int dst, int value, int len);
extern void avanim_play_move_sound(int soundId);
extern unsigned int aveditor_placement_all_valid(void);
extern void aveditor_start_test_match(unsigned long long a1, unsigned long long a2,
                                      unsigned long long a3, unsigned long long a4,
                                      unsigned long long a5, unsigned long long a6,
                                      unsigned long long a7, unsigned long long a8);
extern void aveditor_option_reset_prompt(int screen);
extern void arcsel_label_strings_build(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8);
extern void hud_create_back_dialog(int screen, int msg, int arg);
extern void hud_screen_push(void);

// weapon-config stream/tag helpers (raw)
extern int  FUN_80345da8(int channel);        // channel silence
extern int *FUN_80339b90(int *stream, int tag);
extern int  FUN_80346b5c(void);               // active level stream base

// HUD list-builder primitives
extern void hud_element_init_from_layout(int layoutId, int titleId, int b, void *cb);
extern void hud_set_item_padding_mode(double h, int a, int b, int c);
extern void hud_set_context_property(int flags);
extern void hud_element_scroll_h_overflow(double x, double a, double b, int c);
extern void hud_begin_scroll_with_item(void);
extern void hud_item_add_header(double h, unsigned int locked);
extern void hud_event_add_pair(void);
extern void hud_item_set_linked_value(int *value);
extern void hud_end_scroll_region(void);
extern void hud_add_callback_item(double h);
extern void hud_add_list_item(void);
extern void hud_set_last_child_tag(int tag);
extern void hud_element_apply_scroll(void);
extern void hud_element_finalize_frame(void);
extern void hud_element_set_pos_y(double y, int metric);

// forward decls within this TU
void aveditor_launch_or_prompt(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8, int screen);
void aveditor_init_time_limit(char *block);
int *aveditor_resolve_weapon_config(void);

// 0x8031fec8: validate placement; if any placed object is invalid, raise a warning
// back dialog. Otherwise run the option row's validation callback (if present): on
// success launch the test match, on failure raise the option reset prompt.
void aveditor_launch_or_prompt(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                               unsigned long long a4, unsigned long long a5, unsigned long long a6,
                               unsigned long long a7, unsigned long long a8, int screen)
{
    int msg;
    int (*cb)(void);
    int cbResult;

    if (aveditor_placement_all_valid() == 0)
    {
        avanim_play_move_sound(0x1575);
        msg = get_string_by_id(0x1693);
        hud_create_back_dialog(screen, msg, 0);
        return;
    }

    cb = *(int (**)(void))(&DAT_8047fffc + (unsigned int)DAT_80612ca1 * 0x24);
    if (cb == 0)
    {
        aveditor_start_test_match(a1, a2, a3, a4, a5, a6, a7, a8);
        return;
    }
    cbResult = cb();
    if (cbResult == 0)
    {
        aveditor_start_test_match(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    else
    {
        aveditor_option_reset_prompt(screen);
    }
    return;
}

// 0x8031ff64: dispatch the selected option row (screen+0x188): rows flagged as
// having a sub-screen push it; rows without one go through the launch/prompt path.
void aveditor_dispatch_option_action(unsigned long long a1, unsigned long long a2,
                                     unsigned long long a3, unsigned long long a4,
                                     unsigned long long a5, unsigned long long a6,
                                     unsigned long long a7, unsigned long long a8, int screen)
{
    unsigned int row;

    row = *(unsigned int *)(screen + 0x188);
    DAT_80612ca1 = (int)(unsigned char)row;
    if (*(int *)(&DAT_80480018 + (row & 0xff) * 0x24) == 0)
    {
        aveditor_launch_or_prompt(a1, a2, a3, a4, a5, a6, a7, a8, screen);
    }
    else
    {
        hud_screen_push();
    }
    return;
}

// 0x8031ffb8: initialise a slot block's time-limit field (block+0x58 = minutes*300)
// and clamp its slot count (block+0) to at least 1.
void aveditor_init_time_limit(char *block)
{
    char count;

    *(int *)(block + 0x58) = DAT_806109c0 * 300;
    count = 1;
    if (*block > 0)
    {
        count = *block;
    }
    *block = count;
    return;
}

// 0x8031ffe4: build the bot-count/spawn options screen (layout 0xa800) - a spawn-mode
// header pair bound to DAT_805d59b4 (tag 0x39), a callback item, and a list item.
void aveditor_build_botcount_screen(int screen)
{
    int titleId;

    aveditor_init_time_limit((char *)&DAT_80501570);
    titleId = get_string_by_id(0xa94);
    hud_element_init_from_layout(0xa800, titleId, 0, 0);
    *(int *)(screen + 0x214) = 5;
    hud_set_item_padding_mode((double)FLOAT_8061a834, 1, 1, 1);
    hud_set_context_property(0x20000);
    hud_element_scroll_h_overflow(
        -(double)(FLOAT_80611a5c *
                  ((FLOAT_8061a830 * FLOAT_80611a64 +
                    FLOAT_8061a838 * FLOAT_80611a64 +
                    FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c) -
                  FLOAT_8061a844),
        (double)FLOAT_8061a834, (double)FLOAT_8061a834, 0);

    get_string_by_id(0xa96);
    hud_begin_scroll_with_item();
    hud_item_add_header((double)FLOAT_8061a834, 0);
    hud_set_last_child_tag(0x39);
    get_string_by_id(0xa98);
    hud_event_add_pair();
    get_string_by_id(0xa99);
    hud_event_add_pair();
    hud_item_set_linked_value(&DAT_805d59b4);
    hud_end_scroll_region();

    hud_add_callback_item((double)FLOAT_8061a860);
    get_string_by_id(0xa9e);
    hud_add_list_item();

    hud_element_apply_scroll();
    hud_element_finalize_frame();
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a830 * FLOAT_80611a64 +
                   FLOAT_8061a838 * FLOAT_80611a64 +
                   FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    return;
}

// 0x803202bc: enter a label-editing sub-screen - rebuild the arcade label strings,
// set the "labels dirty" bit (+0x238 |0x100), silence channel 0, then push the
// sub-screen for option rows 0 and 1.
void aveditor_enter_label_screen(unsigned long long a1, unsigned long long a2,
                                 unsigned long long a3, unsigned long long a4,
                                 unsigned long long a5, unsigned long long a6,
                                 unsigned long long a7, unsigned long long a8, int screen)
{
    int row;

    arcsel_label_strings_build(a1, a2, a3, a4, a5, a6, a7, a8);
    *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) | 0x100;
    FUN_80345da8(0);
    row = *(int *)(screen + 0x188);
    if (row == 1)
    {
        hud_screen_push();
    }
    else if ((row < 1) && (row >= 0))
    {
        hud_screen_push();
    }
    return;
}

// 0x80320340: load per-slot tier bytes from the resolved weapon-config block into
// the working slot table (dst+8 stride 0x1c <- block[i].tier at +7 stride 8).
void aveditor_load_weapon_tiers(int dst)
{
    unsigned char *block;
    int dstOff;
    int srcOff;
    int i;

    block = (unsigned char *)aveditor_resolve_weapon_config();
    dstOff = 0;
    srcOff = 0;
    for (i = 10; i != 0; i = i - 1)
    {
        *(unsigned int *)(dst + dstOff + 8) = (unsigned int)block[srcOff + 7];
        dstOff = dstOff + 0x1c;
        srcOff = srcOff + 8;
    }
    return;
}

// 0x80320398: store the working slot table into the resolved weapon-config block:
// weaponId (block+4), tier (block+10), ammo (block+7), and compute the total count
// written back to block[0]. Marshals 10 slots (src stride 0x1c, dst stride 8).
void aveditor_store_weapon_config(int src)
{
    int *block;
    int *slot;
    int total;
    int weaponId;
    int tmp;
    int dstOff;
    int i;

    total = 0;
    block = aveditor_resolve_weapon_config();
    dstOff = 0;
    for (i = 10; i != 0; i = i - 1)
    {
        slot = (int *)(src + (10 - i) * 0x1c);
        *(short *)((int)block + dstOff + 4) = (short)*slot;
        *(short *)((int)block + dstOff + 10) = (short)slot[6];
        weaponId = *slot;
        tmp = -weaponId + -1;
        *(char *)((int)block + dstOff + 7) = (char)slot[2];
        total = total + (tmp - ((unsigned int)(tmp == 0) + -weaponId + -2));
        dstOff = dstOff + 8;
    }
    *block = total;
    return;
}

// 0x80320424: resolve the active weapon-config block. In-editor (DAT_80612614==0x66)
// this is the editor context block at +0x11860; otherwise it is rebuilt from the
// level command stream's tagged block (0xc77668) into the static DAT_805d6454 array.
int *aveditor_resolve_weapon_config(void)
{
    int *block;
    unsigned char *tag;
    int dstOff;
    int srcOff;
    int i;

    if (DAT_80612614 == 0x66)
    {
        block = (int *)(DAT_80612be4 + 0x11860);
    }
    else
    {
        memset_wrapper(-0x7fa29bac, 0, 0xa4);
        tag = (unsigned char *)FUN_80339b90((int *)DAT_80612be8, 0xc77668);
        tag = tag + 0x16;
        DAT_805d6454 = (int)*tag;
        dstOff = 0;
        srcOff = 0;
        for (i = 0; i < DAT_805d6454; i = i + 1)
        {
            *(unsigned short *)((int)&DAT_805d6458 + dstOff) =
                *(unsigned short *)(tag + srcOff + 2);
            *(short *)((int)&DAT_805d645e + dstOff) = (short)(char)tag[srcOff + 5];
            dstOff = dstOff + 8;
            srcOff = srcOff + 4;
        }
        block = &DAT_805d6454;
    }
    return block;
}

// 0x8032016c: dirty check - true when the working weapon-slot config differs from
// the saved baseline in the level stream (10 slots: weaponId at +0x18, tier at +0x1b
// vs baseline entries at +0x11860, stride 8/4).
int aveditor_weapons_config_changed(void)
{
    int *stream;
    unsigned int *baseline;
    int result;
    int a;
    int b;
    int n;

    result = 0;
    stream = (int *)FUN_80346b5c();
    stream = FUN_80339b90(stream, 0xc77668);
    baseline = (unsigned int *)(DAT_80612be4 + 0x11860);
    if ((unsigned int)*(unsigned char *)((int)stream + 0x16) == *baseline)
    {
        a = 0;
        b = 0;
        for (n = 10; n != 0; n = n - 1)
        {
            if ((*(short *)((int)stream + b + 0x18) != *(short *)((int)baseline + a + 4)) ||
                ((int)*(char *)((int)stream + b + 0x1b) != (int)*(short *)((int)baseline + a + 10)))
            {
                return 1;
            }
            a = a + 8;
            b = b + 4;
        }
    }
    else
    {
        result = 1;
    }
    return result;
}

// 0x8032021c: dirty check - true when the working bot config differs from the saved
// baseline in the level stream (15 entries: value at +0x44 stride 2 vs baseline at
// +0x1190c stride 4, count at +0x42 vs +0x11908).
int aveditor_bots_config_changed(void)
{
    int *stream;
    int result;
    int a;
    int b;
    int n;

    result = 0;
    stream = (int *)FUN_80346b5c();
    stream = FUN_80339b90(stream, 0xc77668);
    if ((unsigned int)*(unsigned char *)((int)stream + 0x42) ==
        *(unsigned int *)(DAT_80612be4 + 0x11908))
    {
        a = 0;
        b = 0;
        for (n = 15; n != 0; n = n - 1)
        {
            if ((int)*(short *)((int)stream + b + 0x44) !=
                *(int *)(DAT_80612be4 + a + 0x1190c))
            {
                return 1;
            }
            a = a + 4;
            b = b + 2;
        }
    }
    else
    {
        result = 1;
    }
    return result;
}
