// arcade_edit_preview.cpp
// Arcade map-editor preview-entity management and the main action menu.
// The editor keeps two 0x1d (29) slot preview-entity pointer arrays in its screen
// context: primary at ctx+0x4068 and secondary at ctx+0x40dc. Preview entity type
// ids are selected per game-mode / weapon-set via the DAT_805d59a0 selector; the
// base type id added to the slot index differs by mode (0x7b3, 0x7ed, 2000, 0x844,
// 0x827, 0x80a), mirroring the level-def preload base ids.
//
// Address: 0x8031dc44..0x8031e61c | Batch: batch_899 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern void      *PTR_FUN_804802b0;     // editor screen descriptor / vtable
extern int        DAT_80612be4;         // editor screen context base
extern int        DAT_80612c14;         // active member list head
extern int        DAT_80612c98;
extern int        DAT_80612c9c;
extern int        DAT_805d59a0;          // current game-mode / weapon-set selector
extern int        DAT_8060f948;
extern unsigned char DAT_8047c148;       // placement record table base
extern int        DAT_8047c18c;          // per-context placement record count
extern int        DAT_8047c250;
extern float      FLOAT_8061a830, FLOAT_8061a838, FLOAT_8061a83c;
extern float      FLOAT_80611a5c, FLOAT_80611a64, FLOAT_80611a6c;
extern int        DAT_806119e0;
// preview entity spawn template (position/orientation blocks)
extern unsigned int DAT_803e0a48, DAT_803e0a4c, DAT_803e0a50, DAT_803e0a54, DAT_803e0a58, DAT_803e0a5c;
extern unsigned int DAT_803e0a60, DAT_803e0a64, DAT_803e0a68, DAT_803e0a6c, DAT_803e0a70, DAT_803e0a74;

extern int  get_string_by_id(int stringId);
extern void hud_element_init_from_layout(int a, int b, void *cb1, void *cb2);
extern void hud_set_item_padding_mode(double a, int b, int c, int d);
extern void hud_set_context_property(int prop);
extern void hud_add_list_item(void);
extern void hud_set_last_child_tag(int tag);
extern void hud_lock_child_by_id(int screen, int id, int lock);
extern void hud_element_set_pos_y(double y, int arg);
extern int  member_list_sum_weight_c038(int listHead);
extern int  entity_destroy_with_event(unsigned int *entity);
extern int  entity_spawn_by_type(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                 unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                 unsigned long long a8, int typeId, void *posBlock, void *oriBlock,
                                 int f1, int f2);
extern void music_pause(void);
extern unsigned long long music_resume(void);

extern void aveditor_screen_param_x(int index);
extern void aveditor_dispatch_menu_action(void);
extern void aveditor_leave_screen(void);
extern bool FUN_8034737c(void);
extern bool FUN_803473a0(void);
extern unsigned long long FUN_803826f0(void);
extern void FUN_8038273c(void);
extern void FUN_80349068(unsigned long long a1, double a2, double a3, double a4, unsigned long long a5,
                         unsigned long long a6, unsigned long long a7, unsigned long long a8, int sel);
extern unsigned long long FUN_80341058(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8);

// forward references within this file
void aveditor_destroy_preview_entities(void);
unsigned int aveditor_compute_placement_flags(void);
void aveditor_spawn_preview_entity(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                   unsigned long long a8, int slot);
void aveditor_spawn_secondary_previews(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8);

// 0x8031dc44: build the editor main action list menu (12 items). Locks the second
// item when the map has no members.
void aveditor_build_action_menu(int screen)
{
    int weight;

    *(unsigned int *)(DAT_80612be4 + 0x238) = *(unsigned int *)(DAT_80612be4 + 0x238) & 0xfffffbff;
    aveditor_screen_param_x(DAT_8060f948);
    hud_element_init_from_layout(0x8800, 0, (void *)aveditor_dispatch_menu_action, (void *)aveditor_leave_screen);
    hud_set_item_padding_mode((double)FLOAT_8061a830, 2, 1, 1);
    hud_set_context_property(0x20200);
    *(int *)(screen + 0x214) = 1;

    static const int kMenuStrings[12] = {
        0xaac, 0xa7b, 0xa7c, 0xa7d, 0xa7a, 0xa80, 0xa7e, 0xa7f, 0xa81, 0xa82, 0x1749, 0xa84
    };
    for (int i = 0; i < 12; i++)
    {
        // items 2 and 3 gate on the test-mode readiness checks in the original
        if (i == 2) { (void)FUN_8034737c(); }
        if (i == 3) { (void)FUN_803473a0(); }
        get_string_by_id(kMenuStrings[i]);
        hud_add_list_item();
        hud_set_last_child_tag(i);
    }

    weight = member_list_sum_weight_c038(DAT_80612c14);
    if (weight == 0)
    {
        hud_lock_child_by_id(screen, 1, 1);
    }
    hud_element_set_pos_y(
        (double)(FLOAT_80611a5c *
                 ((FLOAT_8061a830 * FLOAT_80611a64 +
                   FLOAT_8061a838 * FLOAT_80611a64 +
                   FLOAT_8061a83c * FLOAT_80611a64 + FLOAT_80611a6c) - FLOAT_80611a6c)),
        DAT_806119e0);
    return;
}

// 0x8031df20: return the editor screen descriptor / vtable.
void **aveditor_get_screen_desc(void)
{
    return &PTR_FUN_804802b0;
}

// 0x8031dfec: commit the working preview count (DAT_80612c98 -> DAT_80612c9c).
void aveditor_commit_preview_count(void)
{
    DAT_80612c9c = DAT_80612c98;
    return;
}

// 0x8031df80: destroy all 29 primary preview entities (ctx+0x4068 slots).
void aveditor_destroy_preview_entities(void)
{
    unsigned int **slot;
    int off;
    int i;

    FUN_803826f0();
    off = 0;
    for (i = 0; i < 0x1d; i++)
    {
        slot = *(unsigned int ***)(DAT_80612be4 + off + 0x4068);
        if (slot != 0)
        {
            entity_destroy_with_event((unsigned int *)slot);
            *(unsigned int **)(DAT_80612be4 + off + 0x4068) = 0;
        }
        off += 4;
    }
    FUN_8038273c();
    return;
}

// 0x8031dff8: walk the per-context placement record table and compute a flag mask
// from the record type bytes (only records with flag byte[5]==3 contribute).
unsigned int aveditor_compute_placement_flags(void)
{
    unsigned char *rec;
    unsigned char typeByte;
    int base;
    int off;
    int i;
    unsigned int result;

    result = 0;
    base = (int)FUN_803826f0() * 0x140;
    off = 0;
    for (i = 0; i < *(int *)(&DAT_8047c18c + base); i++)
    {
        rec = &DAT_8047c148 + off + 0x48 + base;
        typeByte = *rec;
        if ((typeByte != 5) && (typeByte != 6) && (rec[5] != 2) && (rec[5] == 3))
        {
            // per-type accumulation over rec[4] entries (mask bits by type)
            result |= (unsigned int)(1 << (typeByte & 0x1f));
        }
        off += 6;
    }
    return result;
}

// 0x8031e364: spawn one preview entity for the given slot, using the mode-specific
// base type id from the DAT_805d59a0 selector; destroys any existing slot entity.
void aveditor_spawn_preview_entity(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                   unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                   unsigned long long a8, int slot)
{
    unsigned int *existing;
    unsigned int spawned;
    int off;
    int baseType;
    unsigned char posBlock[24];

    off = slot * 4;
    // copy the spawn template (position/orientation)
    *(unsigned int *)(posBlock + 0)  = DAT_803e0a48;
    *(unsigned int *)(posBlock + 4)  = DAT_803e0a4c;
    *(unsigned int *)(posBlock + 8)  = DAT_803e0a50;
    *(unsigned int *)(posBlock + 12) = DAT_803e0a54;
    *(unsigned int *)(posBlock + 16) = DAT_803e0a58;
    *(unsigned int *)(posBlock + 20) = DAT_803e0a5c;

    existing = *(unsigned int **)(DAT_80612be4 + off + 0x4068);
    if (existing != 0)
    {
        entity_destroy_with_event(existing);
        *(unsigned int **)(DAT_80612be4 + off + 0x4068) = 0;
    }

    baseType = -1;
    switch (DAT_805d59a0)
    {
    case 0: baseType = 0x7b3; break;
    case 1: baseType = 2000;  break;   // 0x7d0
    case 2: baseType = 0x7ed; break;
    case 3: baseType = 0x80a; break;
    case 4: baseType = 0x827; break;
    case 5: baseType = 0x844; break;
    default: break;
    }
    if (baseType >= 0)
    {
        spawned = (unsigned int)entity_spawn_by_type(a1, a2, a3, a4, a5, a6, a7, a8,
                                                     slot + baseType,
                                                     posBlock, posBlock + 12, 0, 0);
        *(unsigned int *)(DAT_80612be4 + off + 0x4068) = spawned;
    }

    if (DAT_805d59a0 == 3)
    {
        unsigned int flags = aveditor_compute_placement_flags();
        int inner = *(int *)(*(int *)(DAT_80612be4 + off + 0x4068) + 4);
        *(unsigned int *)(inner + 0x30) = *(unsigned int *)(inner + 0x30) | flags;
    }
    return;
}

// 0x8031e558: reload a single preview slot (thin wrapper).
void aveditor_reload_preview_slot(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                  unsigned long long a8, int slot)
{
    aveditor_spawn_preview_entity(a1, a2, a3, a4, a5, a6, a7, a8, slot);
    return;
}

// 0x8031e578: reload all 29 preview models (music paused during the model load).
void aveditor_reload_all_previews(unsigned long long a1, double a2, double a3, double a4,
                                  unsigned long long a5, unsigned long long a6,
                                  unsigned long long a7, unsigned long long a8)
{
    int slot;
    int off;

    FUN_803826f0();
    music_pause();
    aveditor_destroy_preview_entities();
    FUN_80349068((unsigned long long)aveditor_destroy_preview_entities, a2, a3, a4, a5, a6, a7, a8, DAT_805d59a0);
    music_resume();

    off = 0;
    for (slot = 0; slot < 0x1d; slot++)
    {
        aveditor_spawn_preview_entity(a1, a2, a3, a4, a5, a6, a7, a8, slot);
        if (DAT_805d59a0 == 3)
        {
            unsigned int flags = aveditor_compute_placement_flags();
            int inner = *(int *)(*(int *)(DAT_80612be4 + off + 0x4068) + 4);
            *(unsigned int *)(inner + 0x30) = *(unsigned int *)(inner + 0x30) | flags;
        }
        off += 4;
    }
    FUN_8038273c();
    return;
}

// 0x8031e61c: spawn the 29 secondary preview entities (ctx+0x40dc, base type 0x7b3)
// then run 0x2d follow-up spawns.
void aveditor_spawn_secondary_previews(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                       unsigned long long a5, unsigned long long a6,
                                       unsigned long long a7, unsigned long long a8)
{
    unsigned int *existing;
    unsigned int spawned;
    int off;
    int slot;
    unsigned char posBlock[24];

    FUN_803826f0();
    *(unsigned int *)(posBlock + 0)  = DAT_803e0a60;
    *(unsigned int *)(posBlock + 4)  = DAT_803e0a64;
    *(unsigned int *)(posBlock + 8)  = DAT_803e0a68;
    *(unsigned int *)(posBlock + 12) = DAT_803e0a6c;
    *(unsigned int *)(posBlock + 16) = DAT_803e0a70;
    *(unsigned int *)(posBlock + 20) = DAT_803e0a74;

    off = 0;
    for (slot = 0; slot < 0x1d; slot++)
    {
        existing = *(unsigned int **)(DAT_80612be4 + off + 0x40dc);
        if (existing != 0)
        {
            entity_destroy_with_event(existing);
            *(unsigned int **)(DAT_80612be4 + off + 0x40dc) = 0;
        }
        spawned = (unsigned int)entity_spawn_by_type(a1, a2, a3, a4, a5, a6, a7, a8,
                                                     slot + 0x7b3, posBlock, posBlock + 12, 0, 0);
        *(unsigned int *)(DAT_80612be4 + off + 0x40dc) = spawned;
        off += 4;
    }
    for (slot = 0; slot < 0x2d; slot++)
    {
        FUN_80341058(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    FUN_8038273c();
    return;
}

// 0x8031df2c: clear both 29-slot preview arrays, then spawn the secondary set.
void aveditor_clear_preview_slots(unsigned long long a1, double a2, double a3, unsigned long long a4,
                                  unsigned long long a5, unsigned long long a6, unsigned long long a7,
                                  unsigned long long a8)
{
    int off;
    int i;

    off = 0;
    for (i = 0x1d; i != 0; i--)
    {
        *(unsigned int *)(DAT_80612be4 + off + 0x4068) = 0;
        *(unsigned int *)(DAT_80612be4 + off + 0x40dc) = 0;
        off += 4;
    }
    aveditor_spawn_secondary_previews(a1, a2, a3, a4, a5, a6, a7, a8);
    return;
}
