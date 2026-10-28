// arcade_edit_selectlist.cpp
// Arcade map-editor weapon-set / game-mode selection list builders, their id-range
// validity predicates, the bot-config resolve/store helpers, and the weapon-set
// select popup screen.
//
// The weapon-set selection is stored in DAT_805d59a4 and the game-mode selection in
// DAT_805d59a8. The bot config lives in the editor context block at
// DAT_80612be4+0x11904 while in-editor (DAT_80612614==0x66), otherwise it is rebuilt
// from the level command stream's tagged block (0xc77668) into DAT_805d64f8.
//
// Address: 0x803204e4..0x803207d8 | Batch: batch_903 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern int  DAT_80612be4;      // editor screen context base
extern int  DAT_80612be8;      // active level command stream base
extern int  DAT_80612614;      // editor-state discriminator (0x66 => in-editor)
extern int  DAT_80612bf4;      // parent screen handle
extern int  DAT_805d59a4;      // weapon-set selection / linked value
extern int  DAT_805d59a8;      // game-mode selection / linked value
extern int  DAT_806111b0;      // weapon-set baseline
extern int  DAT_805d64f8;      // resolved bot-config block base
extern int  DAT_805d64fc;      // resolved bot-config: count
extern int  DAT_805d6500;      // resolved bot-config: entry[0]

extern float FLOAT_8061a834;
extern float FLOAT_8061a864;

// ---- helpers defined elsewhere (raw, per-file externs) ----
extern void memset_wrapper(int dst, int value, int len);
extern int *FUN_80339b90(int *stream, int tag);
extern int  get_string_by_id(int stringId);
extern int  award_validate_id_range(int id);
extern void weapon_loadout_slot_get(int id);
extern void weapon_loadout_flag_get(int id);

extern void aveditor_ensure_models_loaded(unsigned int ctx, unsigned long long a2,
                                          unsigned long long a3, unsigned long long a4,
                                          unsigned long long a5, unsigned long long a6,
                                          unsigned long long a7, unsigned long long a8);
extern void aveditor_enter_screen(void);   // void (see generated_decls.h); defined in arcade_edit_screen_nav.cpp

// HUD list-builder primitives
extern int  hud_get_last_created_item(void);
extern void hud_item_add_header(double h, int flags);
extern void hud_event_add_pair(void);
extern void hud_item_set_linked_value(int *value);
extern void hud_add_list_item(void);
extern void hud_add_callback_item_default(int a);
extern int  hud_style_apply_variant(int variant);
extern int *hud_element_create(int parent, int a, int b);
extern void hud_element_push(int *element);
extern void hud_element_pop(void);
extern void hud_element_init_from_layout(int layoutId, int a, void *cb, int b);
extern void hud_element_apply_focus(int *element);
extern void hud_element_link_and_show(int parent, int element);
extern void hud_input_reset_repeat_timers(void);

// forward decls within this TU
bool aveditor_is_valid_weaponset_id(int id);
bool aveditor_is_valid_gamemode_id(int id);
unsigned char *aveditor_resolve_bot_config(void);
void aveditor_weaponset_select_confirm(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8, int screen);

// 0x803204e4: return the current weapon-set selection.
int aveditor_get_weapon_set(void)
{
    return DAT_805d59a4;
}

// 0x803204f4: weapon-set id validity - id > 1, and (id <= 0xb or id == 0x11).
bool aveditor_is_valid_weaponset_id(int id)
{
    bool valid;

    valid = 1 < id;
    if ((0xb < id) && (id != 0x11))
    {
        valid = false;
    }
    return valid;
}

// 0x80320524: populate the weapon-set list - set the last item's width, add a
// locked header, then add one pair per valid weapon-set id (0..0x11) and bind the
// list to the weapon-set selection value.
void aveditor_build_weaponset_list(void)
{
    int item;
    int id;

    item = hud_get_last_created_item();
    *(float *)(item + 0xe0) = FLOAT_8061a864;
    hud_item_add_header((double)FLOAT_8061a834, 0x30c00);
    id = 0;
    do
    {
        if ((award_validate_id_range(id) != 0) && aveditor_is_valid_weaponset_id(id))
        {
            weapon_loadout_slot_get(id);
            hud_event_add_pair();
        }
        id = id + 1;
    } while (id < 0x12);
    hud_item_set_linked_value(&DAT_805d59a4);
    return;
}

// 0x803205c4: resolve the active bot-config block. In-editor this is the editor
// context block at +0x11904; otherwise it is rebuilt from the level stream tagged
// block (0xc77668) into DAT_805d64f8 (count at +0x42, entries at +0x44 stride 2).
unsigned char *aveditor_resolve_bot_config(void)
{
    int *tag;
    unsigned char *block;
    int dstOff;
    int srcOff;
    int i;

    if (DAT_80612614 == 0x66)
    {
        block = (unsigned char *)(DAT_80612be4 + 0x11904);
    }
    else
    {
        block = (unsigned char *)&DAT_805d64f8;
        memset_wrapper(-0x7fa29b08, 0, 0x44);
        tag = FUN_80339b90((int *)DAT_80612be8, 0xc77668);
        DAT_805d64fc = (int)*(unsigned char *)((int)tag + 0x42);
        dstOff = 0;
        srcOff = 0;
        for (i = 0; i < DAT_805d64fc; i = i + 1)
        {
            *(int *)((int)&DAT_805d6500 + dstOff) =
                (int)*(short *)((unsigned char *)((int)tag + 0x42) + srcOff + 2);
            srcOff = srcOff + 2;
            dstOff = dstOff + 4;
        }
    }
    return block;
}

// 0x8032067c: return the current game-mode selection.
int aveditor_get_game_mode(void)
{
    return DAT_805d59a8;
}

// 0x8032068c: store a value into the resolved bot-config block at entry 'index'
// (block + index*4 + 8).
void aveditor_store_bot_value(int index, int value)
{
    unsigned char *block;

    block = aveditor_resolve_bot_config();
    *(int *)(block + index * 4 + 8) = value;
    return;
}

// 0x803206d0: game-mode id validity - id > 0, and (id <= 9 or id == 0xf).
bool aveditor_is_valid_gamemode_id(int id)
{
    bool valid;

    valid = 0 < id;
    if ((9 < id) && (id != 0xf))
    {
        valid = false;
    }
    return valid;
}

// 0x80320700: populate the game-mode list - locked header, then a pair per valid
// game-mode id (0..0xf), bound to the game-mode selection value.
void aveditor_build_gamemode_list(void)
{
    int id;

    hud_item_add_header((double)FLOAT_8061a834, 0x30c00);
    id = 0;
    do
    {
        if (aveditor_is_valid_gamemode_id(id))
        {
            weapon_loadout_flag_get(id);
            hud_event_add_pair();
        }
        id = id + 1;
    } while (id < 0x10);
    hud_item_set_linked_value(&DAT_805d59a8);
    return;
}

// 0x80320774: confirm the weapon-set selection popup - when a choice was made
// (screen+0x194 != 0), latch it into ctx+0x200 and the baseline, clear two state
// bits, re-enter the editor screen and reload the models.
void aveditor_weaponset_select_confirm(unsigned long long a1, unsigned long long a2,
                                       unsigned long long a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8, int screen)
{
    (void)a1;
    if (*(int *)(screen + 0x194) != 0)
    {
        *(int *)(DAT_80612be4 + 0x200) = *(int *)(screen + 0x188);
        DAT_806111b0 = *(int *)(DAT_80612be4 + 0x200);
        *(unsigned int *)(DAT_80612be4 + 0x25c) =
            *(unsigned int *)(DAT_80612be4 + 0x25c) & 0xfffbffff;
        *(unsigned int *)(DAT_80612be4 + 0x238) =
            *(unsigned int *)(DAT_80612be4 + 0x238) & 0xfffffbff;
        // aveditor_enter_screen is void; its decompiled "return" is a leftover
        // register that becomes the model-load ctx (the editor screen handle).
        aveditor_enter_screen();
        aveditor_ensure_models_loaded((unsigned int)screen, a2, a3, a4, a5, a6, a7, a8);
    }
    return;
}

// 0x803207d8: open the weapon-set select popup - create/push a child list element,
// populate it (title + a callback item + accept/decline items), link it under the
// parent and show it, restoring the prior style variant.
void aveditor_open_weaponset_select(void)
{
    int prevVariant;
    int *element;

    prevVariant = hud_style_apply_variant(5);
    element = hud_element_create(DAT_80612bf4, 0, 2);
    hud_element_push(element);
    hud_element_init_from_layout(0x8000, 0, (void *)aveditor_weaponset_select_confirm, 0);
    get_string_by_id(0xaa7);
    hud_add_list_item();
    hud_add_callback_item_default(0);
    get_string_by_id(0xaa4);
    hud_add_list_item();
    get_string_by_id(0xaa5);
    hud_add_list_item();
    hud_element_pop();
    hud_element_apply_focus(element);
    hud_element_link_and_show(DAT_80612bf4, (int)element);
    hud_style_apply_variant(prevVariant);
    hud_input_reset_repeat_timers();
    return;
}
