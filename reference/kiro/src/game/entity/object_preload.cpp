// object_preload.cpp
// Game object/prop resource preload graph. These orchestrators walk the weapon
// and object-def tables and thread a register-pair "carrier" through successive
// preload/spawn calls to force-load every model, texture set and sound bank a
// level needs. The top-level entry (object_preload_level_resources, 0x801a8290)
// is invoked from game_subsystems_update.
//
// Register-pair accessors: FUN_803826d4/e4/e8 return the current object/type in
// the high word and a mode/flag in the low word; FUN_8038272x close the scope.
//
// Batch: batch_534 | Subsystem: game | Date: 2026-09-07

#include "common.h"

// ---- Object-base helpers (defined in object_base.cpp) ----
extern void *object_prop_preload_or_spawn(void *carrier, double a, double b, void *c,
                                          void *d, void *e, void *f, void *g,
                                          int def_id, int mode);
extern void *object_prop_preload_explosion_set(void *carrier, double a, double b, void *c,
                                               void *d, void *e, void *f, void *g, int mode);
extern void *object_prop_preload_smoke(void *carrier, double a, double b, void *c,
                                       void *d, void *e, void *f, void *g, int mode);
extern int  object_type_dispatch(int arg0, int type_id, int arg2, int arg3, int arg4);
extern int  match_slot_field_ptr(int base, int slot);

// ---- Raw object-type table + config globals ----
extern int DAT_80610ba0;              // object-type table (0x29c stride)
extern int DAT_8042e238;              // per-type param table (0xe0 stride)
extern int DAT_8048ca88;              // weapon-slot def table (0x30 stride, +0 def id u16)
extern int DAT_80501584;              // active weapon-slot index array
extern char DAT_805015bd;             // game-mode sub-flag byte
extern unsigned int DAT_805015c0;     // global game-mode flag word
extern int DAT_80612610;              // active player/slot count
extern int DAT_80612614;              // level/mode id
extern unsigned int DAT_806125fc;     // preload gate mask A
extern unsigned int DAT_80612600;     // preload gate mask B
extern int DAT_805cc614;              // mode sub-state
extern int DAT_8043f298;              // per-entity primary def table (0x54 stride)
extern int DAT_8043f29c;              // per-entity def table (0x54 stride, alt field)
extern int DAT_8043e264;              // per-entity group-id table (0x3c stride; 0xf words = 0x3c)
extern int DAT_8043e22c;              // per-entity flags/def table (0x3c stride)
extern int DAT_8042e2c8;              // weapon-effect def table (0xe0 stride, +0)

// ---- Cross-module preload helpers ----
extern void *sound_clear_bank_ref(int bank_id);
extern void  player_ai_projectile_clear_sound_banks(int obj);
extern void *sprite_load_by_flag_mask(void *carrier, double a, double b, void *c,
                                      void *d, void *e, void *f, void *g);
extern void *sprite_load_texture_set(void *carrier, double a, double b, void *c,
                                     void *d, void *e, void *f, void *g);
extern void *shell_casing_preload_models(void *carrier, double a, double b, void *c,
                                         void *d, void *e, void *f, void *g);
extern void *entity_preload_resources(void *carrier, double a, double b, void *c,
                                      void *d, void *e, void *f, void *g, int def_id);
extern void *spawn_game_entities(void *carrier, double a, double b, void *c,
                                 void *d, void *e, void *f, void *g);
extern int  match_build_weapon_list(void);

// ---- Register-pair accessor stubs (current object/type + mode) ----
extern unsigned long long FUN_803826e8(void);   // returns hi=carrier, lo=mode
extern unsigned long long FUN_803826e4(void);   // returns hi=type, lo=mode (signed test)
extern unsigned long long FUN_803826d4(void);   // returns hi=entity index, lo=mode
extern int  FUN_8022a8a0(void);                  // per-player weapon slot table base
extern unsigned int FUN_801c0894(void);          // preload viewport/mode mask
extern void FUN_80382720(void);
extern void FUN_80382730(void);
extern void FUN_80382734(void);

// 0x801a7b9c: preload the active weapon-slot prop defs. In the special mode
// (flag 0x100000 set, sub-flag '\n', not flag 0x40, level != 100) only 2 slots
// are walked; otherwise DAT_80612610 slots. Def ids come from the weapon-slot
// table DAT_8048ca88 (0x30 stride) indexed by DAT_80501584.
void *object_preload_weapon_slots(void *carrier, double a, double b, void *c,
                                  void *d, void *e, void *f, void *g)
{
    int mode = (int)FUN_803826e8();
    bool special = false;
    if ((DAT_805015c0 & 0x100000) != 0) {
        if ((DAT_805015bd == '\n') && ((DAT_805015c0 & 0x40) == 0) && (DAT_80612614 != 100)) {
            special = true;
        }
    }
    int count = 2;
    if (!special) {
        count = DAT_80612610;
    }
    void *cur = carrier;
    for (int i = 0; i < count; i = i + 1) {
        int slot = (unsigned char)(&DAT_80501584)[i];
        int def_id = *(unsigned short *)((int)&DAT_8048ca88 + slot * 0x30);
        cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, def_id, mode);
    }
    FUN_80382734();
    return cur;
}

// 0x801a7c58: preload all resources for one object type - projectile/impact prop
// defs (base[0..2]), conditional smoke (0x746) and spark (0x74b) effect defs,
// explosion set for effect ids 0x13-0x15, plus (when mode/lo==0) clears sound
// bank refs, dispatches the type init method, and loads texture sets for both
// param channels (base[5]/base[7], DAT_8042e238 0xe0 stride).
void object_preload_type(void *carrier, double a, double b, void *c,
                         void *d, void *e, void *f, void *g,
                         int arg9, int arg10, int skip_props)
{
    (void)arg9;
    (void)arg10;
    long long acc = (long long)FUN_803826e4();
    int type_index = (int)((unsigned long long)acc >> 0x20);
    int lo = (int)acc;
    if (acc < 0) {
        FUN_80382730();
        return;
    }
    int *base = (int *)(DAT_80610ba0 + type_index * 0x29c);
    void *cur = carrier;
    if (skip_props == 0) {
        cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, base[0], lo);
        if (base[0] == -1) {
            cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, base[1], lo);
        }
        cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, base[2], lo);
    }
    if ((DAT_805015bd == '\n') || (DAT_80612614 == 100) || (base[5] == 0x16) || (base[7] == 0x16)) {
        cur = object_prop_preload_smoke(cur, a, b, c, d, e, f, g, lo);
    }
    if ((base[5] == 0x17) || (base[7] == 0x17)) {
        cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g, 0x74b, lo);
    }
    int fx_a = base[5];
    int fx_b = base[7];
    if ((fx_a == 0x13) || (fx_b == 0x13) || (fx_a == 0x14) || (fx_b == 0x14) ||
        (fx_a == 0x15) || (fx_b == 0x15)) {
        object_prop_preload_explosion_set(cur, a, b, c, d, e, f, g, lo);
    }
    if (lo == 0) {
        for (int i = 0; i < 2; i = i + 1) {
            int slot = match_slot_field_ptr((int)base, i);
            bool use_alt = false;
            unsigned int vpmask = FUN_801c0894();
            if (((DAT_806125fc & DAT_80612600 & vpmask & 4) != 0) &&
                ((DAT_80612614 != 0x65) || (DAT_805cc614 == 5))) {
                use_alt = true;
            }
            int *bank = use_alt ? (int *)(slot + 0x40) : (int *)(slot + 0x34);
            sound_clear_bank_ref(bank[0]);
            sound_clear_bank_ref(bank[1]);
            sound_clear_bank_ref(*(int *)(slot + 0x4c));
            player_ai_projectile_clear_sound_banks(*(int *)(slot + 4));
        }
        sound_clear_bank_ref(base[0xc]);
        if (type_index == 0x17) {
            sound_clear_bank_ref(0xcb1);
        }
        if (base[0x15] == 9) {
            cur = sprite_load_by_flag_mask(cur, a, b, c, d, e, f, g);
        }
        object_type_dispatch(0, type_index, 0, 0, 0);
        if (base[5] != 0) {
            cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
            for (int i = 0; i < 4; i = i + 1) {
                int entry = i * 0x20 + base[5] * 0xe0;
                if (*(int *)((int)&DAT_8042e238 + entry + 0xc) == 0) break;
                if (*(int *)((int)&DAT_8042e238 + entry + 0x10) != -1) {
                    cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                }
                if (*(int *)((int)&DAT_8042e238 + entry + 0x14) != -1) {
                    cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                }
            }
        }
        if ((base[7] != 0) && (base[7] != base[5])) {
            cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
            for (int i = 0; i < 4; i = i + 1) {
                int entry = i * 0x20 + base[7] * 0xe0;
                if (*(int *)((int)&DAT_8042e238 + entry + 0xc) == 0) break;
                if (*(int *)((int)&DAT_8042e238 + entry + 0x10) != -1) {
                    cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                }
                if (*(int *)((int)&DAT_8042e238 + entry + 0x14) != -1) {
                    cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                }
            }
        }
    }
    FUN_80382730();
}

// 0x801a7f70: preload one entity's weapon defs. Walks the per-entity def table
// (DAT_8043f298, 0x54 stride) and, sharing group ids (DAT_8043e264), preloads
// each referenced object type (object_preload_type) for both channels.
void *object_preload_entity_weapons(void *carrier, double a, double b, void *c,
                                    void *d, void *e, void *f, void *g)
{
    void *cur = carrier;
    long long acc = (long long)FUN_803826d4();
    int idx = (int)((unsigned long long)acc >> 0x20);
    int mode = (int)acc;
    if ((acc >= 0x100000000LL) && (acc < 0x4600000000LL)) {
        cur = object_prop_preload_or_spawn(carrier, a, b, c, d, e, f, g,
                        *(int *)((int)&DAT_8043f298 + idx * 0x54), mode);
        if (mode == 0) {
            int off = 0;
            for (int j = 0; j < 0x46; j = j + 1) {
                if ((&DAT_8043e264)[idx * 0xf] == *(int *)((int)&DAT_8043e264 + off)) {
                    if (((*(unsigned int *)((int)&DAT_8043e22c + off) & 4) == 0) ||
                        (((&DAT_8043e22c)[idx * 0xf] & 4) != 0)) {
                        cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                        cur = sprite_load_texture_set(cur, a, b, c, d, e, f, g);
                        int ch_off = 0;
                        for (int k = 0; k < 2; k = k + 1) {
                            cur = (void *)0;
                            object_preload_type(cur, a, b, c, d, e, f, g,
                                *(int *)((int)&DAT_8043e22c + off + ch_off + 0xc), 0, 0);
                            ch_off = ch_off + 4;
                        }
                    }
                }
                off = off + 0x3c;
            }
        }
        int ch_off = 0;
        for (int k = 0; k < 2; k = k + 1) {
            object_preload_type(cur, a, b, c, d, e, f, g,
                *(int *)((int)(&DAT_8043e22c + idx * 0xf) + ch_off + 0xc), mode, 0);
            ch_off = ch_off + 4;
        }
    }
    FUN_80382720();
    return cur;
}

// 0x801a80ac: preload the explosion effect set (mode 0).
void *object_preload_explosion_set_default(void *carrier, double a, double b, void *c,
                                           void *d, void *e, void *f, void *g)
{
    return object_prop_preload_explosion_set(carrier, a, b, c, d, e, f, g, 0);
}

// 0x801a80d0: preload the smoke effect (mode 0).
void *object_preload_smoke_default(void *carrier, double a, double b, void *c,
                                   void *d, void *e, void *f, void *g)
{
    return object_prop_preload_smoke(carrier, a, b, c, d, e, f, g, 0);
}

// 0x801a80f4: preload one type, skipping the base prop defs (skip_props=1).
void object_preload_type_no_props(void *carrier, double a, double b, void *c,
                                  void *d, void *e, void *f, void *g, int type)
{
    object_preload_type(carrier, a, b, c, d, e, f, g, type, 0, 1);
}

// 0x801a811c: preload one type including base prop defs (skip_props=0).
void object_preload_type_full(void *carrier, double a, double b, void *c,
                              void *d, void *e, void *f, void *g, int type)
{
    object_preload_type(carrier, a, b, c, d, e, f, g, type, 0, 0);
}

// 0x801a8144: preload the current match weapon set. Builds the weapon list,
// preloads the active weapon slots, then per-player weapon defs (flags 0x40 /
// 0x8000000), the match weapon list entries, and a trailing entity-weapon pass.
void object_preload_match_weapons(void *carrier, double a, double b, void *c,
                                  void *d, void *e, void *f, void *g)
{
    (void)carrier;
    int weapon_list = match_build_weapon_list();
    void *cur = object_preload_weapon_slots((void *)0, a, b, c, d, e, f, g);
    if (((DAT_805015c0 & 0x40) != 0) || ((DAT_805015c0 & 0x8000000) != 0)) {
        int slot_off = 0;
        for (int i = 0; (i < DAT_80612610) && (i < 3); i = i + 1) {
            int tbl = FUN_8022a8a0();
            cur = (void *)0;
            if (*(int *)(tbl + slot_off + 0x74) != -1) {
                cur = object_preload_entity_weapons((void *)0, a, b, c, d, e, f, g);
            }
            slot_off = slot_off + 0xc;
        }
    }
    if (weapon_list != 0) {
        for (int i = 0; i < *(int *)(weapon_list + 4); i = i + 1) {
            cur = object_preload_entity_weapons(cur, a, b, c, d, e, f, g);
        }
    }
    object_preload_entity_weapons(cur, a, b, c, d, e, f, g);
}

// 0x801a8228: preload shell-casing models + match weapons, gated by game mode.
void object_preload_shell_and_weapons(void *carrier, double a, double b, void *c,
                                      void *d, void *e, void *f, void *g)
{
    if (((DAT_805015c0 & 0x100000) == 0) ||
        (((DAT_80612614 != 100) && (DAT_80612614 != 0x66) &&
          ((DAT_805015c0 & 0x40) == 0) && (DAT_805015bd == '\n')))) {
        void *cur = shell_casing_preload_models(carrier, a, b, c, d, e, f, g);
        object_preload_match_weapons(cur, a, b, c, d, e, f, g);
    }
}

// 0x801a8290: top-level level resource preload (from game_subsystems_update).
// Preloads the weapon-effect def table (DAT_8042e2c8, 0xe0 stride, 0x2b entries),
// per-entity weapon defs + object defs (DAT_8043e22c/DAT_8043f29c), the default
// explosion/smoke effect sets, shell casings, weapon slots, then spawns game
// entities.
void object_preload_level_resources(void *carrier, double a, double b, void *c,
                                    void *d, void *e, void *f, void *g)
{
    (void)carrier;
    void *cur = (void *)FUN_803826e8();
    int off = 0xe0;
    for (int i = 1; i < 0x2c; i = i + 1) {
        cur = entity_preload_resources(cur, a, b, c, d, e, f, g,
                  *(int *)((int)&DAT_8042e2c8 + off));
        // secondary effect def at entry+0x04 (0x8042e2cc base = DAT_8042e2c8 + 4)
        if (*(int *)((int)&DAT_8042e2c8 + off + 4) != -1) {
            cur = entity_preload_resources(cur, a, b, c, d, e, f, g,
                      *(int *)((int)&DAT_8042e2c8 + off + 4));
        }
        off = off + 0xe0;
    }
    int flag_off = 0;
    int def_off = 0;
    for (int i = 0; i < 0x45; i = i + 1) {
        if ((*(unsigned int *)((int)&DAT_8043e22c + flag_off) & 1) == 0) {
            cur = object_preload_entity_weapons(cur, a, b, c, d, e, f, g);
            cur = object_prop_preload_or_spawn(cur, a, b, c, d, e, f, g,
                      *(int *)((int)&DAT_8043f29c + def_off), 0);
        }
        flag_off = flag_off + 0x3c;
        def_off = def_off + 0x54;
    }
    cur = object_preload_explosion_set_default(cur, a, b, c, d, e, f, g);
    cur = object_preload_smoke_default(cur, a, b, c, d, e, f, g);
    cur = shell_casing_preload_models(cur, a, b, c, d, e, f, g);
    cur = object_preload_weapon_slots(cur, a, b, c, d, e, f, g);
    spawn_game_entities(cur, a, b, c, d, e, f, g);
    FUN_80382734();
}
