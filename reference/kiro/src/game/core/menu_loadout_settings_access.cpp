// menu_loadout_settings_access.cpp - loadout slot/settings accessors.
//
// This family resolves "where does the setting for slot N live?" for the
// loadout/gamemode-select UI. Every accessor branches on the active game
// mode: mode 0x66 ("online"/proxied mode) routes through a separate settings
// block (the FUN_80320xxx family), otherwise the value is read directly out
// of the local game settings struct returned by game_get_settings_ptr().
//
//   menu_slot_is_selectable_ex   0x802ca12c - slot selectable predicate (mode9 or !last & !=0xb).
//   menu_slot_get_weapon_setting 0x802ca188 - pointer to a slot's weapon setting.
//   menu_get_active_slot_index   0x802ca1ec - current active slot/category index.
//   menu_get_child_slot_index    0x802ca224 - active slot index for a child element.
//   menu_get_variant_setting     0x802ca260 - resolve the current variant/game-length setting.
//   menu_get_child_variant       0x802ca2e4 - variant setting for a child element.
//   menu_get_team_setting_ptr    0x802ca320 - pointer to the current team setting block.
//   menu_settings_push_defaults  0x802ca388 - copy the 10-entry default table INTO settings.
//   menu_settings_pull_defaults  0x802ca4bc - copy the 10-entry table back OUT of settings.
//   menu_map_option_table_build  0x802ca5fc - build the per-map option table from map metadata.
//
// Address range: 0x802ca12c - 0x802ca6cb
// Batch: batch_803

#include "common.h"

// ---- file-local externs for raw symbols (per Compile-Clean Standard) ----

// Mode-0x66 (proxied/online) settings-block accessor family.
extern "C" {
    extern int*  FUN_80320424(void);   // weapon-setting pointer (mode 0x66)
    extern int   FUN_803204e4(void);   // active slot index (mode 0x66)
    extern int   FUN_8032067c(void);   // variant setting (mode 0x66)
    extern void  FUN_80320398(int tag);// push defaults (mode 0x66)
    extern void  FUN_80320340(int ctx);// pull defaults (mode 0x66)
    extern char* FUN_803205c4(void);   // team-setting pointer (mode 0x66)
    extern int*  FUN_80339b90(void* handle, int key); // resource/record lookup
}

// Frame scratch-context enter/leave markers (compiler-emitted stack helpers).
extern int  menu_scratch_ctx_begin(void); // FUN_803826e4
extern void menu_scratch_ctx_end(void);   // FUN_80382730

// Named cross-module helpers (this project declares them per-file; common.h
// does not pull in generated_decls.h). Signatures match generated_decls.h.
extern "C" {
    extern void* game_get_settings_ptr(void);
    extern int   mode_variant_count_remaining(void);
    extern char  mode_slot_high_group(int slot);
    extern int*  hud_find_child_by_id(int element, int target_id);
    extern unsigned int menu_slot_is_last(int slot);
    extern int   menu_slot_is_last_in_mode9(int slot);
    extern int   memset_wrapper(int dst, int val, int len);
}

// Raw data symbols.
extern int   DAT_805cc614;   // active gamemode id (secondary copy)
extern int   DAT_80612614;   // active gamemode id (primary copy; 0x66 == online)
extern void* DAT_80612be8;   // active map/record handle
extern int   DAT_805cc744;   // default table: slot id column
extern int   DAT_805cc748;   // default table: field @0x700
extern int   DAT_805cc74c;   // default table: field @0x6d8
extern int   DAT_805cc750;   // default table: field @0x6b0
extern int   DAT_805cc75c;   // default table: field @0x728
extern unsigned char DAT_8048f97c; // static per-slot option table base
extern unsigned int  DAT_805cc85c; // scratch option-count
extern int   DAT_805cc860;   // scratch option table base

// ---- 0x802ca12c: slot selectable predicate --------------------------------
// Returns 1 when the slot can be selected: either the mode is 9, or the slot
// is neither the terminal slot nor slot 0xb.
int menu_slot_is_selectable_ex(int slot)
{
    if (DAT_805cc614 == 9)
        return 1;
    if (menu_slot_is_last(slot) == 0 && slot != 0xb)
        return 1;
    return 0;
}

// ---- 0x802ca188: pointer to a slot's weapon setting -----------------------
int* menu_slot_get_weapon_setting(int slot)
{
    if (DAT_80612614 == 0x66 || menu_slot_is_last_in_mode9(slot) != 0)
        return (int*)FUN_80320424();

    char* settings = (char*)game_get_settings_ptr();
    return (int*)(settings + (slot - 0xc) * 0xa4 + 0x2cc);
}

// ---- 0x802ca1ec: current active slot/category index -----------------------
int menu_get_active_slot_index(void)
{
    if (DAT_80612614 == 0x66)
        return FUN_803204e4();

    char* settings = (char*)game_get_settings_ptr();
    return *(int*)(settings + 0x6a8);
}

// ---- 0x802ca224: active slot index for a child element --------------------
int menu_get_child_slot_index(int element)
{
    if (DAT_80612614 == 0x66)
        return menu_get_active_slot_index();

    int* child = hud_find_child_by_id(element, -2);
    return *(int*)((char*)child + 200);
}

// ---- 0x802ca260: resolve the current variant/game-length setting ----------
int menu_get_variant_setting(void)
{
    if (DAT_80612614 == 0x66)
        return FUN_8032067c();

    if (DAT_80612614 == 100 &&
        DAT_805cc614 != 9 && DAT_805cc614 != 0xc &&
        DAT_805cc614 != 0xd && DAT_805cc614 != 0xb)
    {
        int* rec = FUN_80339b90(DAT_80612be8, 0xc77668);
        return (int)*(short*)((char*)rec + 0x10);
    }

    char* settings = (char*)game_get_settings_ptr();
    return *(int*)(settings + 0x6a4);
}

// ---- 0x802ca2e4: variant setting for a child element ----------------------
int menu_get_child_variant(int element)
{
    if (DAT_80612614 == 0x66)
        return menu_get_variant_setting();

    int* child = hud_find_child_by_id(element, -5);
    return *(int*)((char*)child + 200);
}

// ---- 0x802ca320: pointer to the current team setting block ----------------
char* menu_get_team_setting_ptr(void)
{
    int slot = menu_get_variant_setting();

    if (DAT_80612614 == 0x66 || mode_slot_high_group(slot) != '\0')
        return FUN_803205c4();

    char* settings = (char*)game_get_settings_ptr();
    return settings + (slot - 10) * 0x44 + 0x134;
}

// ---- 0x802ca388: copy the 10-entry default table INTO the settings block --
// Walks up to 10 variant entries and writes the default option values into the
// active slot's row of the settings struct (or defers to the mode-0x66 path).
void menu_settings_push_defaults(void)
{
    menu_scratch_ctx_begin();

    char* settings = (char*)game_get_settings_ptr();
    if (DAT_80612614 == 0x66)
    {
        FUN_80320398(-0x7fa338bc);
    }
    else
    {
        int src = 0;   // byte offset into DAT_805cc744.. source table
        int col = 0;   // 4-byte column stride
        int row = 0;   // 8-byte row stride
        for (int i = 0; i < 10; ++i)
        {
            int slotIdx = *(int*)(settings + 0x6a8);
            if (i < mode_variant_count_remaining() && 0xb < slotIdx)
            {
                *(short*)(settings + slotIdx * 0xa4 + row - 0x4e0) =
                    (short)*(int*)((char*)&DAT_805cc744 + src);
                *(int*)(settings + slotIdx * 0xa0 + col + 0x728) =
                    *(int*)((char*)&DAT_805cc75c + src);
            }
            if (*(int*)(settings + 0x10) == 0)
            {
                *(int*)(settings + slotIdx * 0xa0 + col + 0x6d8) =
                    *(int*)((char*)&DAT_805cc74c + src);
            }
            *(int*)(settings + slotIdx * 0xa0 + col + 0x6b0) =
                *(int*)((char*)&DAT_805cc750 + src);
            *(int*)(settings + slotIdx * 0xa0 + col + 0x700) =
                *(int*)((char*)&DAT_805cc748 + src);
            row += 8;
            col += 4;
            src += 0x1c;
        }
    }

    menu_scratch_ctx_end();
}

// ---- 0x802ca4bc: copy the 10-entry table back OUT of the settings block ---
// Inverse of menu_settings_push_defaults: reads the active slot's row out of
// the settings struct into the DAT_805cc744.. staging table.
void menu_settings_pull_defaults(void)
{
    int ctx = menu_scratch_ctx_begin();

    if (DAT_80612614 == 0x66)
    {
        FUN_80320340(ctx);
    }
    else
    {
        char* settings = (char*)game_get_settings_ptr();
        int dst = 0;   // 0x1c stride into staging table
        int col = 0;   // 4-byte column stride
        int row = 0;   // 8-byte row stride
        for (int i = 0; i < 10; ++i)
        {
            if (i < mode_variant_count_remaining())
            {
                int slotIdx = *(int*)(settings + 0x6a8);
                if (0xb < slotIdx && DAT_805cc614 != 9)
                {
                    *(int*)((char*)&DAT_805cc744 + dst) =
                        (int)*(short*)(settings + slotIdx * 0xa4 + row - 0x4e0);
                    *(int*)((char*)&DAT_805cc75c + dst) =
                        *(int*)(settings + slotIdx * 0xa0 + col + 0x728);
                }
                if (*(int*)(settings + 0x10) == 0)
                {
                    *(int*)((char*)&DAT_805cc74c + dst) =
                        *(int*)(settings + slotIdx * 0xa0 + col + 0x6d8);
                }
                *(int*)((char*)&DAT_805cc750 + dst) =
                    *(int*)(settings + slotIdx * 0xa0 + col + 0x6b0);
                *(int*)((char*)&DAT_805cc748 + dst) =
                    *(int*)(settings + slotIdx * 0xa0 + col + 0x700);
            }
            dst += 0x1c;
            row += 8;
            col += 4;
        }
    }

    menu_scratch_ctx_end();
}

// ---- 0x802ca5fc: build the per-map option table from map metadata ---------
// If the map advertises < 0xc options, returns a pointer into the static
// per-slot table (DAT_8048f97c). Otherwise it parses the map record's option
// blob into the scratch table (DAT_805cc85c count + DAT_805cc860 entries).
int* menu_map_option_table_build(void)
{
    int* rec = FUN_80339b90(DAT_80612be8, 0xc77668);

    if (*(short*)((char*)rec + 5) < 0xc)
    {
        int idx = *(short*)((char*)rec + 5);
        return (int*)(&DAT_8048f97c + (idx + 0x1b) * 0xa4);
    }

    unsigned char* blob = (unsigned char*)((char*)rec + 0x16);
    memset_wrapper(-0x7fa337a4, 0, 0xa4);
    DAT_805cc85c = (unsigned int)*blob;

    int dst = 0;
    int src = 0;
    for (unsigned int i = 0; i < DAT_805cc85c; ++i)
    {
        *(unsigned short*)((char*)&DAT_805cc860 + dst) =
            *(unsigned short*)(blob + src + 2);
        *(short*)((char*)&DAT_805cc860 + dst + 6) =
            (short)(char)blob[src + 5];
        dst += 8;
        src += 4;
    }
    return (int*)&DAT_805cc85c;
}
