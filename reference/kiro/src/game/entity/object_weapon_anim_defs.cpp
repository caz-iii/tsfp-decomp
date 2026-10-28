// object_weapon_anim_defs.cpp
// Per-weapon-class animation-DESCRIPTOR registration table + a reaction/death
// anim selector. Address range 0x801bb84c - 0x801bbca8. Continues the weapon
// animation subsystem (object_weapon_anim.cpp).
//
// Each register function is a thin load/release wrapper: mode 0 releases a
// contiguous run of anim descriptor slots (anim_release_descriptors(slotArray,
// count)); any other mode loads that run from the shared ts3 anim data folder
// via anim_load_descriptor_list, keyed by a per-class descriptor name table
// (PTR_s_mingun_idle1, PTR_s_r110_run, ...). The public entry points are the
// small "_load" thunks that call the worker with mode=1. object_weapon_select_
// reaction_anim (0x801bb84c) is the odd one out: it picks the appropriate
// hit/death reaction anim pair for the current object and installs it via
// anim_set_pair_9498.
//
// Non-matching: the 0x43300000 int->double bias idiom (Ghidra int->double) is
// reduced to ordinary math; the wide 16-arg FP-context PPC ABI tail is carried
// verbatim so the anim_load_descriptor_list call sites keep matching the ROM
// convention. Raw field-offset pointer arithmetic on the object / model
// structures is preserved (layouts not yet typed).
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Object-type descriptor table + tuning constants for the reaction selector.
extern int    DAT_80610ba0;    // weapon-type descriptor table base (stride 0x29c)
extern double DOUBLE_80616e38; // int->double bias (rng)
extern float  FLOAT_80616e2c;  // rng normalize divisor
extern float  FLOAT_80616e0c;  // reaction-anim probability A
extern float  FLOAT_80616e30;  // reaction-anim probability B

// Shared anim-data folder path + per-class descriptor name tables.
extern char s_anim_data_ts3__80441448[];
extern char PTR_s_mingun_idle1_803cd1b0[];
extern char PTR_s_r110_run_803cd390[];
extern char PTR_s_monkeydanceloop01_803cd768[];
extern char PTR_s_futuresg_fire_803cd7c8[];
extern char PTR_s_zomambleidle_803cd978[];
} // extern "C"

// anim_set_pair_9498 (0x80024130) installs a {mask, anim-id} pair on an object;
// resolves via the permissive variadic extern in generated_externs.h.

// anim_load_descriptor_list / anim_release_descriptors are declared with narrow
// 3-arg signatures in generated_decls, but the ROM calls them with the wide
// FP-context PPC ABI. Call them through correctly-typed pointers to their ROM
// addresses to preserve the real call shape.
typedef void (*AnimLoadListFn)(unsigned long long, double, double, unsigned long long,
                               unsigned long long, unsigned long long, unsigned long long,
                               unsigned long long, const char *nameFolder, const char *nameTable,
                               int count, unsigned int, unsigned int, unsigned int, unsigned int,
                               unsigned int);
static const AnimLoadListFn __ow_anim_load_descriptor_list = (AnimLoadListFn)0x80022398;
static inline void __ow_anim_release_descriptors(int slotArray, int count)
{
    ((void (*)(int, int))0x80022354)(slotArray, count);
}
// rng_counter_tick is void in generated_decls; recover its u32 counter via a
// typed pointer to its ROM address.
static inline unsigned int __ow_rng_tick(void)
{
    return ((unsigned int (*)(void))0x8013104c)();
}

// ===========================================================================
// Address: 0x801bb84c | Name: object_weapon_select_reaction_anim | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Choose and install the hit/death reaction animation pair for the object
// referenced by param_1 (an anim-context whose *param_1 -> object at +0x7c).
// Falls back to a generic reaction (0x6e) when the object has no live weapon
// model, otherwise picks a light/heavy/special reaction (0x69-0x6e) by weapon
// type flags (DAT_80610ba0 descriptor +0xc bits 1/0x100), grabbed state
// (+0x150), a per-request forced-anim override (param_1[0x26]), and an
// rng-weighted coin flip. All installed via anim_set_pair_9498(mask 0x1ff8/
// 0x1f8, animId).
// ===========================================================================
void object_weapon_select_reaction_anim(int *param_1)
{
    int *obj = *(int **)(*param_1 + 0x7c);
    if ((obj[0x20] == 0) || (obj[0x25] == 0)) {
        int subtype = obj[0x25];
        if ((subtype == 0x38) ||
            ((unsigned int)(subtype - 0x2b) < 2) || ((unsigned int)(subtype - 0x2e) < 2) ||
            (subtype == 0x28)) {
            anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x71);
        } else if ((obj[5] & 0x60000U) == 0) {
            if ((obj[0xaf] == 0) || (obj[99] == 0)) {
                unsigned int flags = *(unsigned int *)(DAT_80610ba0 + obj[0x81] * 0x29c + 0xc);
                if ((flags & 1) == 0) {
                    if (((flags & 0x100) != 0) ||
                        (((obj[0x2fc] == 0 || ((*(unsigned int *)(obj[0x2fc] + 0x54) & 0x800) == 0)) &&
                          ((param_1[0x26] != 0x6a) &&
                           (((float)((double)__ow_rng_tick() - DOUBLE_80616e38) / FLOAT_80616e2c <= FLOAT_80616e0c) ||
                            (param_1[0x26] == 0x6c)))))) {
                        anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6c);
                    } else {
                        anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6a);
                    }
                } else if (((flags & 0x100) != 0) ||
                           (((obj[0x2fc] == 0 || ((*(unsigned int *)(obj[0x2fc] + 0x54) & 0x800) == 0)) &&
                             (param_1[0x26] != 0x69) &&
                             (((float)((double)__ow_rng_tick() - DOUBLE_80616e38) / FLOAT_80616e2c <= FLOAT_80616e30) ||
                              (param_1[0x26] == 0x6b)) &&
                             (*(int *)(*obj + 0x150) == 0)))) {
                    anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6b);
                } else {
                    anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x69);
                }
            } else if (subtype < 0xe) {
                anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6e);
            } else {
                anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6d);
            }
        } else {
            anim_set_pair_9498((int)(intptr_t)param_1, 0x1f8, 0x6b);
        }
    } else {
        anim_set_pair_9498((int)(intptr_t)param_1, 0x1ff8, 0x6e);
    }
}

// ===========================================================================
// Address: 0x801bbad8 | Name: object_anim_defs_mingun | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load (mode!=0) or release (mode==0) the 0x14-entry minigun anim descriptor
// run from the shared ts3 anim data folder (PTR_s_mingun_idle1 table).
// ===========================================================================
void object_anim_defs_mingun(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043d1b0, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_mingun_idle1_803cd1b0, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbb28 | Name: object_anim_defs_mingun_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the minigun anim descriptors (object_anim_defs_mingun mode 1).
// ===========================================================================
void object_anim_defs_mingun_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_mingun(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbb4c | Name: object_anim_defs_run | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x29-entry humanoid locomotion anim descriptor run
// (PTR_s_r110_run table).
// ===========================================================================
void object_anim_defs_run(unsigned long long param_1, double param_2, double param_3,
                          unsigned long long param_4, unsigned long long param_5,
                          unsigned long long param_6, unsigned long long param_7,
                          unsigned long long param_8, int mode, unsigned int param_10,
                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043d390, 0x29);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_r110_run_803cd390, 0x29,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbb9c | Name: object_anim_defs_run_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the run/locomotion anim descriptors (object_anim_defs_run mode 1).
// ===========================================================================
void object_anim_defs_run_load(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_run(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                         1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbbc0 | Name: object_anim_defs_monkeydance | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 4-entry monkey-dance easter-egg anim descriptor run
// (PTR_s_monkeydanceloop01 table).
// ===========================================================================
void object_anim_defs_monkeydance(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043d768, 4);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_monkeydanceloop01_803cd768, 4,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbc10 | Name: object_anim_defs_monkeydance_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the monkey-dance anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_monkeydance_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_monkeydance(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbc34 | Name: object_anim_defs_futuresg | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry future-shotgun anim descriptor run
// (PTR_s_futuresg_fire table).
// ===========================================================================
void object_anim_defs_futuresg(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043d7c8, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_futuresg_fire_803cd7c8, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbc84 | Name: object_anim_defs_futuresg_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the future-shotgun anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_futuresg_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_futuresg(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbca8 | Name: object_anim_defs_zombie | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x15-entry zombie/ambient-creature anim descriptor run
// (PTR_s_zomambleidle table).
// ===========================================================================
void object_anim_defs_zombie(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043d978, 0x15);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomambleidle_803cd978, 0x15,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_563 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bbcf8 - 0x801bbeec. Same load/release pattern as above:
// mode 0 releases a contiguous descriptor-slot run (anim_release_descriptors),
// any other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_basebat_idle_803cdb70[];
extern char PTR_s_ts_bolt_firefrom_803cdd20[];
extern char PTR_s_mine_to_803cdf48[];
extern char PTR_s_assault_1shot_803ce278[];
extern char PTR_s_pumpaction_fire_803ce4b8[];
} // extern "C"

// ===========================================================================
// Address: 0x801bbcf8 | Name: object_anim_defs_zombie_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the zombie/ambient-creature anim descriptors
// (object_anim_defs_zombie mode 1). Worker defined above (0x801bbca8).
// ===========================================================================
void object_anim_defs_zombie_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zombie(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbd1c | Name: object_anim_defs_basebat | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry baseball-bat anim descriptor run
// (PTR_s_basebat_idle table, slots at 0x8043db70).
// ===========================================================================
void object_anim_defs_basebat(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043db70, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_basebat_idle_803cdb70, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbd6c | Name: object_anim_defs_basebat_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the baseball-bat anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_basebat_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_basebat(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbd90 | Name: object_anim_defs_tsbolt | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x17-entry TS-bolt (crossbow bolt) anim descriptor run
// (PTR_s_ts_bolt_firefrom table, slots at 0x8043dd20).
// ===========================================================================
void object_anim_defs_tsbolt(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043dd20, 0x17);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ts_bolt_firefrom_803cdd20, 0x17,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbde0 | Name: object_anim_defs_tsbolt_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the TS-bolt anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_tsbolt_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_tsbolt(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbe04 | Name: object_anim_defs_mine | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x22-entry mine/proximity-device anim descriptor run
// (PTR_s_mine_to table, slots at 0x8043df48).
// ===========================================================================
void object_anim_defs_mine(unsigned long long param_1, double param_2, double param_3,
                           unsigned long long param_4, unsigned long long param_5,
                           unsigned long long param_6, unsigned long long param_7,
                           unsigned long long param_8, int mode, unsigned int param_10,
                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043df48, 0x22);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_mine_to_803cdf48, 0x22,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbe54 | Name: object_anim_defs_mine_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the mine anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_mine_load(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_mine(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                          1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbe78 | Name: object_anim_defs_assault | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x18-entry assault-rifle anim descriptor run
// (PTR_s_assault_1shot table, slots at 0x8043e278).
// ===========================================================================
void object_anim_defs_assault(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043e278, 0x18);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_assault_1shot_803ce278, 0x18,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbec8 | Name: object_anim_defs_assault_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the assault-rifle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_assault_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_assault(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbeec | Name: object_anim_defs_pumpaction | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x14-entry pump-action-shotgun anim descriptor run
// (PTR_s_pumpaction_fire table, slots at 0x8043e4b8).
// ===========================================================================
void object_anim_defs_pumpaction(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043e4b8, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_pumpaction_fire_803ce4b8, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_564 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bbf3c - 0x801bc130. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_plasma_firesolo_803ce698[];
extern char PTR_s_drivetruck_803ce848[];
extern char PTR_s_monkey_beat_803cead0[];
extern char PTR_s_monkeydanceloop01_803ceb00[]; // distinct table from the 0x803cd768 one above
extern char PTR_s_leaderpeakidle_v01_803cebf0[];
} // extern "C"

// ===========================================================================
// Address: 0x801bbf3c | Name: object_anim_defs_pumpaction_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the pump-action-shotgun anim descriptors
// (object_anim_defs_pumpaction mode 1). Worker defined above (0x801bbeec).
// ===========================================================================
void object_anim_defs_pumpaction_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_pumpaction(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbf60 | Name: object_anim_defs_plasma | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry plasma-weapon anim descriptor run
// (PTR_s_plasma_firesolo table, slots at 0x8043e698).
// ===========================================================================
void object_anim_defs_plasma(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043e698, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_plasma_firesolo_803ce698, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bbfb0 | Name: object_anim_defs_plasma_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the plasma-weapon anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_plasma_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_plasma(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bbfd4 | Name: object_anim_defs_drivetruck | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x1b-entry driveable-truck anim descriptor run
// (PTR_s_drivetruck table, slots at 0x8043e848).
// ===========================================================================
void object_anim_defs_drivetruck(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043e848, 0x1b);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_drivetruck_803ce848, 0x1b,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc024 | Name: object_anim_defs_drivetruck_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the driveable-truck anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_drivetruck_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_drivetruck(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc048 | Name: object_anim_defs_monkeybeat | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 2-entry monkey-beat easter-egg anim descriptor run
// (PTR_s_monkey_beat table, slots at 0x8043ead0).
// ===========================================================================
void object_anim_defs_monkeybeat(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043ead0, 2);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_monkey_beat_803cead0, 2,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc098 | Name: object_anim_defs_monkeybeat_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the monkey-beat anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_monkeybeat_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_monkeybeat(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc0bc | Name: object_anim_defs_monkeydance2 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release a second 10-entry monkey-dance anim descriptor run
// (PTR_s_monkeydanceloop01 table at 0x803ceb00 - distinct from the earlier
// 0x803cd768 monkeydance table, slots at 0x8043eb00).
// ===========================================================================
void object_anim_defs_monkeydance2(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043eb00, 10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_monkeydanceloop01_803ceb00, 10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc10c | Name: object_anim_defs_monkeydance2_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the second monkey-dance anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_monkeydance2_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_monkeydance2(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc130 | Name: object_anim_defs_leaderpeak | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0xf-entry leader/peak-idle anim descriptor run
// (PTR_s_leaderpeakidle_v01 table, slots at 0x8043ebf0).
// ===========================================================================
void object_anim_defs_leaderpeak(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043ebf0, 0xf);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_leaderpeakidle_v01_803cebf0, 0xf,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_565 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bc180 - 0x801bc374. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_BF_Z2_search1_803ced58[];
extern char PTR_s_flight_cycle_803ceec0[];
extern char PTR_s_rebelmalearmcheck_803ceed8[];
extern char PTR_s_giantrob_walkcycle_803cefc8[];
extern char PTR_s_11_peekboxlft_803cf040[];
} // extern "C"

// ===========================================================================
// Address: 0x801bc180 | Name: object_anim_defs_leaderpeak_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the leader/peak-idle anim descriptors
// (object_anim_defs_leaderpeak mode 1). Worker defined above (0x801bc130).
// ===========================================================================
void object_anim_defs_leaderpeak_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_leaderpeak(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc1a4 | Name: object_anim_defs_zsearch | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0xf-entry zombie-search (BF_Z2_search) anim descriptor
// run (PTR_s_BF_Z2_search1 table, slots at 0x8043ed58).
// ===========================================================================
void object_anim_defs_zsearch(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043ed58, 0xf);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_BF_Z2_search1_803ced58, 0xf,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc1f4 | Name: object_anim_defs_zsearch_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the zombie-search anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_zsearch_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zsearch(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc218 | Name: object_anim_defs_flight | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 1-entry flight-cycle anim descriptor run
// (PTR_s_flight_cycle table, slots at 0x8043eec0).
// ===========================================================================
void object_anim_defs_flight(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043eec0, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flight_cycle_803ceec0, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc268 | Name: object_anim_defs_flight_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the flight-cycle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_flight_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flight(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc28c | Name: object_anim_defs_rebelmale | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 10-entry rebel-male (arm-check NPC) anim descriptor run
// (PTR_s_rebelmalearmcheck table, slots at 0x8043eed8).
// ===========================================================================
void object_anim_defs_rebelmale(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043eed8, 10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_rebelmalearmcheck_803ceed8, 10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc2dc | Name: object_anim_defs_rebelmale_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the rebel-male anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_rebelmale_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_rebelmale(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc300 | Name: object_anim_defs_giantrob | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 5-entry giant-robot anim descriptor run
// (PTR_s_giantrob_walkcycle table, slots at 0x8043efc8).
// ===========================================================================
void object_anim_defs_giantrob(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043efc8, 5);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_giantrob_walkcycle_803cefc8, 5,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc350 | Name: object_anim_defs_giantrob_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the giant-robot anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_giantrob_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_giantrob(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc374 | Name: object_anim_defs_peekbox | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x33-entry peek-box (cover-peek set) anim descriptor run
// (PTR_s_11_peekboxlft table, slots at 0x8043f040).
// ===========================================================================
void object_anim_defs_peekbox(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043f040, 0x33);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_peekboxlft_803cf040, 0x33,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_566 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bc3c4 - 0x801bc5b8. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_electro_idle_803cf508[];
extern char PTR_s_zomdeath00_803cf6b8[];
extern char PTR_s_generatorsoldieridle_803cfd90[];
extern char PTR_s_monkeyincellsidle_803d00c0[];
extern char PTR_s_flaregun_reload_803d0288[];
} // extern "C"

// ===========================================================================
// Address: 0x801bc3c4 | Name: object_anim_defs_peekbox_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the peek-box anim descriptors (object_anim_defs_peekbox
// mode 1). Worker defined above (0x801bc374).
// ===========================================================================
void object_anim_defs_peekbox_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_peekbox(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc3e8 | Name: object_anim_defs_electro | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry electro-tool anim descriptor run
// (PTR_s_electro_idle table, slots at 0x8043f508).
// ===========================================================================
void object_anim_defs_electro(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043f508, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_electro_idle_803cf508, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc438 | Name: object_anim_defs_electro_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the electro-tool anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_electro_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_electro(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc45c | Name: object_anim_defs_zomdeath | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x49-entry zombie-death anim descriptor run
// (PTR_s_zomdeath00 table, slots at 0x8043f6b8).
// ===========================================================================
void object_anim_defs_zomdeath(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043f6b8, 0x49);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomdeath00_803cf6b8, 0x49,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc4ac | Name: object_anim_defs_zomdeath_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the zombie-death anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_zomdeath_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zomdeath(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc4d0 | Name: object_anim_defs_gensoldier | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x22-entry generator-soldier NPC anim descriptor run
// (PTR_s_generatorsoldieridle table, slots at 0x8043fd90).
// ===========================================================================
void object_anim_defs_gensoldier(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x8043fd90, 0x22);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_generatorsoldieridle_803cfd90, 0x22,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc520 | Name: object_anim_defs_gensoldier_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the generator-soldier anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_gensoldier_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_gensoldier(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc544 | Name: object_anim_defs_monkeycell | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x13-entry monkey-in-cells anim descriptor run
// (PTR_s_monkeyincellsidle table, slots at 0x804400c0).
// ===========================================================================
void object_anim_defs_monkeycell(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x804400c0, 0x13);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_monkeyincellsidle_803d00c0, 0x13,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc594 | Name: object_anim_defs_monkeycell_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the monkey-in-cells anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_monkeycell_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_monkeycell(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc5b8 | Name: object_anim_defs_flaregun | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x2d-entry flare-gun anim descriptor run
// (PTR_s_flaregun_reload table, slots at 0x80440288).
// ===========================================================================
void object_anim_defs_flaregun(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x80440288, 0x2d);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flaregun_reload_803d0288, 0x2d,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_567 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bc608 - 0x801bc7fc. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_homing_fire_803d06c0[];
extern char PTR_s_gondola_803d0930[];
extern char PTR_s_start_ambient1_reb_803d0978[];
extern char PTR_s_standfromknee_m0_803d0b40[];
extern char PTR_s_femguardconvers01_803d0d98[];
// batch_568 tables (0x801bc870-0x801bca40)
extern char PTR_s_sollookdials_803d1068[];
extern char PTR_s_drunkguardendidle_803d16f8[];
extern char PTR_s_drunkguardendidle_803d1890[];
extern char PTR_s_deskslider01_803d1e78[];
extern char PTR_s_magnum_idle_803d2118[];
// batch_569 tables (0x801bcab4-0x801bcc84)
extern char PTR_s_rifle_idle_803d2328[];
extern char PTR_s_11_tippergunaway_803d2538[];
extern char PTR_s_standpose_m0_803d2700[];
extern char PTR_s_gondola_803d2e20[];
extern char PTR_s_train_ambient_cam_803d2e68[];
// batch_570 tables (0x801bccf8-0x801bcec8)
extern char PTR_s_brick_idle_803d3180[];
extern char PTR_s_look_m0_803d3330[];
extern char PTR_s_irisdoors_small_open_803d39c0[];
extern char PTR_s_luger_shot_803d3ba0[];
extern char PTR_s_plaspist_idle1_803d4068[];
// batch_571 tables (0x801bcf3c-0x801bd10c)
extern char PTR_s_sbp500_idle_803d44a0[];
extern char PTR_s_dozer_start_803d4650[];
extern char PTR_s_06_drunkguard_803d6858[];
extern char PTR_s_machinepistol_32shot_803d6948[];
extern char PTR_s_zomambleattckboth_803d6e70[];
// batch_572 tables (0x801bd180-0x801bd350)
extern char PTR_s_scientistscaredidle_803d72a8[];
extern char PTR_s_11_tippertalk_803d7698[];
extern char PTR_s_shotgunback_803d7a10[];
extern char PTR_s_11_alertguy_803d8448[];
extern char PTR_s_strapflaps_803d84c0[];
// batch_573 tables (0x801bd3c4-0x801bd594)
extern char PTR_s_11_peekboxlft_803d8aa8[];
extern char PTR_s_11_peekboxlft_803d8cd0[];
extern char PTR_s_harpoon_fire_803d8d60[];
extern char PTR_s_fsniper_shot_803d8ee0[];
extern char PTR_s_Heatseeker_Fire_803d90c0[];
// batch_574 tables (0x801bd608-0x801bd7d8)
extern char PTR_s_jbcoweridle_803d9270[];
extern char PTR_s_doublesg_fire_803d9288[];
extern char PTR_s_jobethcaseyhands_texboned_b_803d9438[];
extern char PTR_s_jbarmsflappingidle_803d9450[];
extern char PTR_s_jungleqcellblast_803d99c0[];
// batch_575 tables (0x801bd84c-0x801bda1c)
extern char PTR_s_drowningdeath01_803d9b88[];
extern char PTR_s_Kneelreact_803d9c90[];
extern char PTR_s_r110_drcontwalk_803da620[];
extern char PTR_s_flight_cycle_803da728[];
extern char PTR_s_ashcowerrun_803da7d0[];
// batch_576 tables (0x801bda90-0x801bdc60)
extern char PTR_s_flight_cycle_803da908[];
extern char PTR_s_ashcowerrun_803dabd8[];
extern char PTR_s_mattackheadbutt_803dabf0[];
extern char PTR_s_monkeyiceskit_803dad70[];
extern char PTR_s_girlroachattack_803dada0[];
// batch_577 tables (0x801bdcd4-0x801bdea4)
extern char PTR_s_magcharge_idle_803dae48[];
extern char PTR_s_devgirlheadshake_803db028[];
extern char PTR_s_jbeverybodyout_803db178[];
extern char PTR_s_uplinkidle_803db328[];
extern char PTR_s_zomgroundspawn_803db748[];
} // extern "C"

// ===========================================================================
// Address: 0x801bc608 | Name: object_anim_defs_flaregun_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the flare-gun anim descriptors (object_anim_defs_flaregun
// mode 1). Worker defined above (0x801bc5b8).
// ===========================================================================
void object_anim_defs_flaregun_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flaregun(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc62c | Name: object_anim_defs_homing | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x1a-entry homing-launcher anim descriptor run
// (PTR_s_homing_fire table, slots at 0x803d06c0).
// ===========================================================================
void object_anim_defs_homing(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d06c0, 0x1a);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_homing_fire_803d06c0, 0x1a,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc67c | Name: object_anim_defs_homing_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the homing-launcher anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_homing_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_homing(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc6a0 | Name: object_anim_defs_gondola | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 3-entry gondola/vehicle anim descriptor run
// (PTR_s_gondola table, slots at 0x803d0930).
// ===========================================================================
void object_anim_defs_gondola(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d0930, 3);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_gondola_803d0930, 3,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc6f0 | Name: object_anim_defs_gondola_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the gondola anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_gondola_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_gondola(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc714 | Name: object_anim_defs_rebelambient | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x13-entry rebel-ambient NPC anim descriptor run
// (PTR_s_start_ambient1_reb table, slots at 0x803d0978).
// ===========================================================================
void object_anim_defs_rebelambient(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d0978, 0x13);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_start_ambient1_reb_803d0978, 0x13,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc764 | Name: object_anim_defs_rebelambient_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the rebel-ambient anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_rebelambient_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_rebelambient(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc788 | Name: object_anim_defs_maleconvers | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x19-entry male-conversation NPC anim descriptor run
// (PTR_s_standfromknee_m0 table, slots at 0x803d0b40).
// ===========================================================================
void object_anim_defs_maleconvers(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d0b40, 0x19);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_standfromknee_m0_803d0b40, 0x19,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc7d8 | Name: object_anim_defs_maleconvers_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the male-conversation anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_maleconvers_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_maleconvers(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc7fc | Name: object_anim_defs_femguard | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x1e-entry female-guard-conversation NPC anim descriptor
// run (PTR_s_femguardconvers01 table, slots at 0x803d0d98).
// ===========================================================================
void object_anim_defs_femguard(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d0d98, 0x1e);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_femguardconvers01_803d0d98, 0x1e,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_568: continuation of the per-class anim-descriptor load/release
// wrappers. Address range 0x801bc84c - 0x801bca40. Same idiom as batch_567:
// each "worker" takes an int mode - mode 0 releases the descriptor run
// (__ow_anim_release_descriptors(slotBase, count)), any other mode loads it
// (__ow_anim_load_descriptor_list(..., s_anim_data_ts3, PTR_s_<name>, count,
// ...)). The small "_load" thunks are the public entry points (call the
// worker with mode=1). NON-MATCHING: the wide 16-arg float ABI tail is carried
// verbatim.
// ===========================================================================

// ===========================================================================
// Address: 0x801bc84c | Name: object_anim_defs_femguard_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the female-guard-conversation anim descriptors. Thunk to
// object_anim_defs_femguard (0x801bc7fc, batch_567) with mode=1.
// ===========================================================================
void object_anim_defs_femguard_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_femguard(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc870 | Name: object_anim_defs_sollookdials | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x46-entry soldier-look-at-dials NPC anim descriptor run
// (PTR_s_sollookdials table, slots at 0x803d1068).
// ===========================================================================
void object_anim_defs_sollookdials(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d1068, 0x46);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_sollookdials_803d1068, 0x46,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc8c0 | Name: object_anim_defs_sollookdials_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the soldier-look-at-dials anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_sollookdials_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_sollookdials(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc8e4 | Name: object_anim_defs_drunkguardend_a | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x11-entry drunk-guard-end idle anim descriptor run,
// variant A (PTR_s_drunkguardendidle table, slots at 0x803d16f8).
// ===========================================================================
void object_anim_defs_drunkguardend_a(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d16f8, 0x11);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_drunkguardendidle_803d16f8, 0x11,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc934 | Name: object_anim_defs_drunkguardend_a_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the drunk-guard-end idle anim descriptors, variant A (mode 1).
// ===========================================================================
void object_anim_defs_drunkguardend_a_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_drunkguardend_a(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc958 | Name: object_anim_defs_drunkguardend_b | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x3f-entry drunk-guard-end idle anim descriptor run,
// variant B (PTR_s_drunkguardendidle table, slots at 0x803d1890).
// ===========================================================================
void object_anim_defs_drunkguardend_b(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d1890, 0x3f);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_drunkguardendidle_803d1890, 0x3f,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bc9a8 | Name: object_anim_defs_drunkguardend_b_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the drunk-guard-end idle anim descriptors, variant B (mode 1).
// ===========================================================================
void object_anim_defs_drunkguardend_b_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_drunkguardend_b(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bc9cc | Name: object_anim_defs_deskslider | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x1c-entry desk-slider prop anim descriptor run
// (PTR_s_deskslider01 table, slots at 0x803d1e78).
// ===========================================================================
void object_anim_defs_deskslider(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d1e78, 0x1c);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_deskslider01_803d1e78, 0x1c,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bca1c | Name: object_anim_defs_deskslider_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry: load the desk-slider anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_deskslider_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_deskslider(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bca40 | Name: object_anim_defs_magnum | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x16-entry magnum idle anim descriptor run
// (PTR_s_magnum_idle table, slots at 0x803d2118). Worker only in this batch;
// its "_load" thunk (0x801bca90) is captured by a later batch.
// ===========================================================================
void object_anim_defs_magnum(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2118, 0x16);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_magnum_idle_803d2118, 0x16,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_569: continuation of the per-class anim-descriptor load/release
// wrappers. Address range 0x801bca90 - 0x801bcc84. Same idiom as batch_567/568.
// ===========================================================================

// ===========================================================================
// Address: 0x801bca90 | Name: object_anim_defs_magnum_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the magnum idle anim descriptors by calling
// object_anim_defs_magnum (0x801bca40, batch_568) with mode=1.
// ===========================================================================
void object_anim_defs_magnum_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_magnum(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcab4 | Name: object_anim_defs_rifle | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x16-entry rifle idle anim descriptor run
// (PTR_s_rifle_idle table, slots at 0x803d2328).
// ===========================================================================
void object_anim_defs_rifle(unsigned long long param_1, double param_2, double param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8, int mode, unsigned int param_10,
                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2328, 0x16);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_rifle_idle_803d2328, 0x16,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcb04 | Name: object_anim_defs_rifle_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the rifle idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_rifle_load(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_rifle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcb28 | Name: object_anim_defs_tippergunaway | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x13-entry "tipper gun away" NPC anim descriptor run
// (PTR_s_11_tippergunaway table, slots at 0x803d2538).
// ===========================================================================
void object_anim_defs_tippergunaway(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2538, 0x13);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_tippergunaway_803d2538, 0x13,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcb78 | Name: object_anim_defs_tippergunaway_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the tipper-gun-away anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_tippergunaway_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_tippergunaway(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcb9c | Name: object_anim_defs_standpose | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x4c-entry male stand-pose NPC anim descriptor run
// (PTR_s_standpose_m0 table, slots at 0x803d2700).
// ===========================================================================
void object_anim_defs_standpose(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2700, 0x4c);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_standpose_m0_803d2700, 0x4c,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcbec | Name: object_anim_defs_standpose_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the male stand-pose anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_standpose_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_standpose(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcc10 | Name: object_anim_defs_gondola2 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 3-entry gondola/vehicle anim descriptor run, second table
// (PTR_s_gondola table, slots at 0x803d2e20; distinct from the batch_567
// gondola at 0x803d0930).
// ===========================================================================
void object_anim_defs_gondola2(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2e20, 3);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_gondola_803d2e20, 3,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcc60 | Name: object_anim_defs_gondola2_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the second gondola anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_gondola2_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_gondola2(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcc84 | Name: object_anim_defs_trainambient | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x21-entry train-ambient camera anim descriptor run
// (PTR_s_train_ambient_cam table, slots at 0x803d2e68). Worker only in this
// batch; its "_load" thunk (0x801bccd4) is captured by a later batch.
// ===========================================================================
void object_anim_defs_trainambient(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d2e68, 0x21);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_train_ambient_cam_803d2e68, 0x21,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_570 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bccd4 - 0x801bcec8. Same load/release pattern as the
// prior batches: mode 0 releases a contiguous descriptor-slot run
// (anim_release_descriptors(slots,count)); any other mode loads it from the
// shared ts3 anim-data folder via anim_load_descriptor_list keyed by a
// per-class name table. The small "_load" thunks are the public entry points
// (call the worker with mode=1). The release slot base equals the table
// address for each class. This batch opens with the trainambient "_load"
// thunk deferred from batch_569 (worker at 0x801bcc84).
// ===========================================================================

// ===========================================================================
// Address: 0x801bccd4 | Name: object_anim_defs_trainambient_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the train-ambient camera anim descriptors
// (object_anim_defs_trainambient mode 1). Worker defined above (0x801bcc84).
// ===========================================================================
void object_anim_defs_trainambient_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_trainambient(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bccf8 | Name: object_anim_defs_brick | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry brick object anim descriptor run
// (PTR_s_brick_idle table, slots at 0x803d3180).
// ===========================================================================
void object_anim_defs_brick(unsigned long long param_1, double param_2, double param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8, int mode, unsigned int param_10,
                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d3180, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_brick_idle_803d3180, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcd48 | Name: object_anim_defs_brick_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the brick anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_brick_load(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_brick(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcd6c | Name: object_anim_defs_look_m0 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x46-entry male look/idle NPC anim descriptor run
// (PTR_s_look_m0 table, slots at 0x803d3330).
// ===========================================================================
void object_anim_defs_look_m0(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d3330, 0x46);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_look_m0_803d3330, 0x46,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcdbc | Name: object_anim_defs_look_m0_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the male look/idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_look_m0_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_look_m0(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcde0 | Name: object_anim_defs_irisdoors | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x14-entry iris-door object anim descriptor run
// (PTR_s_irisdoors_small_open table, slots at 0x803d39c0).
// ===========================================================================
void object_anim_defs_irisdoors(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d39c0, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_irisdoors_small_open_803d39c0, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bce30 | Name: object_anim_defs_irisdoors_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the iris-door anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_irisdoors_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_irisdoors(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bce54 | Name: object_anim_defs_luger | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x33-entry luger pistol anim descriptor run
// (PTR_s_luger_shot table, slots at 0x803d3ba0).
// ===========================================================================
void object_anim_defs_luger(unsigned long long param_1, double param_2, double param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8, int mode, unsigned int param_10,
                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d3ba0, 0x33);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_luger_shot_803d3ba0, 0x33,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcea4 | Name: object_anim_defs_luger_load | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Public entry (thunk): load the luger pistol anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_luger_load(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_luger(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcec8 | Name: object_anim_defs_plaspist | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Load / release the 0x2d-entry plasma-pistol anim descriptor run
// (PTR_s_plaspist_idle1 table, slots at 0x803d4068). Worker only in this
// batch; its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_plaspist(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d4068, 0x2d);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_plaspist_idle1_803d4068, 0x2d,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_571 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bcf18 - 0x801bd10c. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors(slots,
// count)); any other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1). The
// release slot base equals the table address for each class. This batch opens
// with the plaspist "_load" thunk deferred from batch_570 (worker at
// 0x801bcec8).
// ===========================================================================

// ===========================================================================
// Address: 0x801bcf18 | Name: object_anim_defs_plaspist_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the plasma-pistol anim descriptors
// (object_anim_defs_plaspist mode 1). Worker defined above (0x801bcec8).
// ===========================================================================
void object_anim_defs_plaspist_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_plaspist(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcf3c | Name: object_anim_defs_sbp500 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry SBP500 pistol anim descriptor run
// (PTR_s_sbp500_idle table, slots at 0x803d44a0).
// ===========================================================================
void object_anim_defs_sbp500(unsigned long long param_1, double param_2, double param_3,
                             unsigned long long param_4, unsigned long long param_5,
                             unsigned long long param_6, unsigned long long param_7,
                             unsigned long long param_8, int mode, unsigned int param_10,
                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d44a0, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_sbp500_idle_803d44a0, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bcf8c | Name: object_anim_defs_sbp500_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the SBP500 anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_sbp500_load(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_sbp500(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                            1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bcfb0 | Name: object_anim_defs_dozer | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x16b-entry dozer/vehicle NPC anim descriptor run
// (PTR_s_dozer_start table, slots at 0x803d4650). Largest table in the file.
// ===========================================================================
void object_anim_defs_dozer(unsigned long long param_1, double param_2, double param_3,
                            unsigned long long param_4, unsigned long long param_5,
                            unsigned long long param_6, unsigned long long param_7,
                            unsigned long long param_8, int mode, unsigned int param_10,
                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d4650, 0x16b);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_dozer_start_803d4650, 0x16b,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd000 | Name: object_anim_defs_dozer_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the dozer anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_dozer_load(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_dozer(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                           1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd024 | Name: object_anim_defs_drunkguard | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 10-entry drunk-guard NPC anim descriptor run
// (PTR_s_06_drunkguard table, slots at 0x803d6858; distinct from the batch_568
// drunkguardend tables).
// ===========================================================================
void object_anim_defs_drunkguard(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d6858, 10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_06_drunkguard_803d6858, 10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd074 | Name: object_anim_defs_drunkguard_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the drunk-guard anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_drunkguard_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_drunkguard(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd098 | Name: object_anim_defs_machinepistol | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x37-entry machine-pistol anim descriptor run
// (PTR_s_machinepistol_32shot table, slots at 0x803d6948).
// ===========================================================================
void object_anim_defs_machinepistol(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d6948, 0x37);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_machinepistol_32shot_803d6948, 0x37,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd0e8 | Name: object_anim_defs_machinepistol_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the machine-pistol anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_machinepistol_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_machinepistol(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd10c | Name: object_anim_defs_zomambleattack | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x2d-entry zombie-amble-attack anim descriptor run
// (PTR_s_zomambleattckboth table, slots at 0x803d6e70). Worker only in this
// batch; its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_zomambleattack(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d6e70, 0x2d);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomambleattckboth_803d6e70, 0x2d,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_572 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bd15c - 0x801bd350. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors(slots,
// count)); any other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1). The
// release slot base equals the table address for each class. This batch opens
// with the zomambleattack "_load" thunk deferred from batch_571 (worker at
// 0x801bd10c).
// ===========================================================================

// ===========================================================================
// Address: 0x801bd15c | Name: object_anim_defs_zomambleattack_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the zombie-amble-attack anim descriptors
// (object_anim_defs_zomambleattack mode 1). Worker defined above (0x801bd10c).
// ===========================================================================
void object_anim_defs_zomambleattack_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zomambleattack(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd180 | Name: object_anim_defs_scientistscared | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x2a-entry scared-scientist NPC anim descriptor run
// (PTR_s_scientistscaredidle table, slots at 0x803d72a8).
// ===========================================================================
void object_anim_defs_scientistscared(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d72a8, 0x2a);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_scientistscaredidle_803d72a8, 0x2a,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd1d0 | Name: object_anim_defs_scientistscared_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the scared-scientist anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_scientistscared_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_scientistscared(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd1f4 | Name: object_anim_defs_tippertalk | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x25-entry tipper-talk conversation anim descriptor run
// (PTR_s_11_tippertalk table, slots at 0x803d7698).
// ===========================================================================
void object_anim_defs_tippertalk(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d7698, 0x25);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_tippertalk_803d7698, 0x25,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd244 | Name: object_anim_defs_tippertalk_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the tipper-talk anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_tippertalk_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_tippertalk(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd268 | Name: object_anim_defs_shotgunback | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x6d-entry shotgun-on-back weapon anim descriptor run
// (PTR_s_shotgunback table, slots at 0x803d7a10).
// ===========================================================================
void object_anim_defs_shotgunback(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d7a10, 0x6d);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_shotgunback_803d7a10, 0x6d,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd2b8 | Name: object_anim_defs_shotgunback_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the shotgun-on-back anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_shotgunback_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_shotgunback(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd2dc | Name: object_anim_defs_alertguy | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 5-entry alert-guard NPC anim descriptor run
// (PTR_s_11_alertguy table, slots at 0x803d8448).
// ===========================================================================
void object_anim_defs_alertguy(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d8448, 5);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_alertguy_803d8448, 5,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd32c | Name: object_anim_defs_alertguy_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the alert-guard anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_alertguy_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_alertguy(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd350 | Name: object_anim_defs_strapflaps | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x3f-entry strap-flaps object anim descriptor run
// (PTR_s_strapflaps table, slots at 0x803d84c0). Worker only in this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_strapflaps(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d84c0, 0x3f);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_strapflaps_803d84c0, 0x3f,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_573 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bd3a0 - 0x801bd594. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors(slots,
// count)); any other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1). The
// release slot base equals the table address for each class. This batch opens
// with the strapflaps "_load" thunk deferred from batch_572 (worker at
// 0x801bd350). The two peekboxlft variants share the same ROM label prefix at
// distinct addresses (0x803d8aa8 / 0x803d8cd0).
// ===========================================================================

// ===========================================================================
// Address: 0x801bd3a0 | Name: object_anim_defs_strapflaps_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the strap-flaps anim descriptors
// (object_anim_defs_strapflaps mode 1). Worker defined above (0x801bd350).
// ===========================================================================
void object_anim_defs_strapflaps_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_strapflaps(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd3c4 | Name: object_anim_defs_peekboxlft | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x17-entry peek-box (left) anim descriptor run
// (PTR_s_11_peekboxlft table at 0x803d8aa8).
// ===========================================================================
void object_anim_defs_peekboxlft(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d8aa8, 0x17);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_peekboxlft_803d8aa8, 0x17,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd414 | Name: object_anim_defs_peekboxlft_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the peek-box (left) anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_peekboxlft_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_peekboxlft(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd438 | Name: object_anim_defs_peekboxlft2 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 6-entry peek-box (left, variant 2) anim descriptor run
// (PTR_s_11_peekboxlft table at 0x803d8cd0; distinct table from 0x803d8aa8).
// ===========================================================================
void object_anim_defs_peekboxlft2(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d8cd0, 6);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_11_peekboxlft_803d8cd0, 6,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd488 | Name: object_anim_defs_peekboxlft2_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the peek-box (left, variant 2) descriptors (mode 1).
// ===========================================================================
void object_anim_defs_peekboxlft2_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_peekboxlft2(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd4ac | Name: object_anim_defs_harpoon | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry harpoon-gun anim descriptor run
// (PTR_s_harpoon_fire table, slots at 0x803d8d60).
// ===========================================================================
void object_anim_defs_harpoon(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d8d60, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_harpoon_fire_803d8d60, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd4fc | Name: object_anim_defs_harpoon_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the harpoon-gun anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_harpoon_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_harpoon(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd520 | Name: object_anim_defs_fsniper | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x14-entry future-sniper-rifle anim descriptor run
// (PTR_s_fsniper_shot table, slots at 0x803d8ee0).
// ===========================================================================
void object_anim_defs_fsniper(unsigned long long param_1, double param_2, double param_3,
                              unsigned long long param_4, unsigned long long param_5,
                              unsigned long long param_6, unsigned long long param_7,
                              unsigned long long param_8, int mode, unsigned int param_10,
                              unsigned int param_11, unsigned int param_12, unsigned int param_13,
                              unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d8ee0, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_fsniper_shot_803d8ee0, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bd570 | Name: object_anim_defs_fsniper_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the future-sniper anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_fsniper_load(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_fsniper(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                             1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bd594 | Name: object_anim_defs_heatseeker | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry heatseeker-launcher anim descriptor run
// (PTR_s_Heatseeker_Fire table, slots at 0x803d90c0). Worker only in this
// batch; its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_heatseeker(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d90c0, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_Heatseeker_Fire_803d90c0, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd5e4 | Name: object_anim_defs_heatseeker_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the heatseeker-launcher anim descriptors (mode 1).
// Worker object_anim_defs_heatseeker was decompiled in batch_573.
// ===========================================================================
void object_anim_defs_heatseeker_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_heatseeker(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd608 | Name: object_anim_defs_jbcoweridle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 1-entry JB-cower-idle anim descriptor run
// (PTR_s_jbcoweridle table, slots at 0x803d9270).
// ===========================================================================
void object_anim_defs_jbcoweridle(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9270, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jbcoweridle_803d9270, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd658 | Name: object_anim_defs_jbcoweridle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the JB-cower-idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_jbcoweridle_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jbcoweridle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd67c | Name: object_anim_defs_doublesg | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry double-shotgun anim descriptor run
// (PTR_s_doublesg_fire table, slots at 0x803d9288).
// ===========================================================================
void object_anim_defs_doublesg(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9288, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_doublesg_fire_803d9288, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd6cc | Name: object_anim_defs_doublesg_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the double-shotgun anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_doublesg_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_doublesg(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd6f0 | Name: object_anim_defs_jobethcaseyhands | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 1-entry Jo-Beth-Casey-hands anim descriptor run
// (PTR_s_jobethcaseyhands_texboned_b table, slots at 0x803d9438).
// ===========================================================================
void object_anim_defs_jobethcaseyhands(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, int mode, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9438, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jobethcaseyhands_texboned_b_803d9438, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd740 | Name: object_anim_defs_jobethcaseyhands_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the Jo-Beth-Casey-hands anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_jobethcaseyhands_load(unsigned long long param_1, double param_2, double param_3,
                                            unsigned long long param_4, unsigned long long param_5,
                                            unsigned long long param_6, unsigned long long param_7,
                                            unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jobethcaseyhands(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                      1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd764 | Name: object_anim_defs_jbarmsflapping | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x3a-entry JB-arms-flapping anim descriptor run
// (PTR_s_jbarmsflappingidle table, slots at 0x803d9450).
// ===========================================================================
void object_anim_defs_jbarmsflapping(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9450, 0x3a);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jbarmsflappingidle_803d9450, 0x3a,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd7b4 | Name: object_anim_defs_jbarmsflapping_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the JB-arms-flapping anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_jbarmsflapping_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jbarmsflapping(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd7d8 | Name: object_anim_defs_jungleqcellblast | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x13-entry jungle-Q-cell-blast anim descriptor run
// (PTR_s_jungleqcellblast table, slots at 0x803d99c0). Worker only in this
// batch; its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_jungleqcellblast(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, int mode, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d99c0, 0x13);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jungleqcellblast_803d99c0, 0x13,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd828 | Name: object_anim_defs_jungleqcellblast_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the jungle-Q-cell-blast anim descriptors (mode 1).
// Worker object_anim_defs_jungleqcellblast was decompiled in batch_574.
// ===========================================================================
void object_anim_defs_jungleqcellblast_load(unsigned long long param_1, double param_2, double param_3,
                                            unsigned long long param_4, unsigned long long param_5,
                                            unsigned long long param_6, unsigned long long param_7,
                                            unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jungleqcellblast(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                      1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd84c | Name: object_anim_defs_drowningdeath | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xb-entry drowning-death anim descriptor run
// (PTR_s_drowningdeath01 table, slots at 0x803d9b88).
// ===========================================================================
void object_anim_defs_drowningdeath(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9b88, 0xb);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_drowningdeath01_803d9b88, 0xb,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd89c | Name: object_anim_defs_drowningdeath_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the drowning-death anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_drowningdeath_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_drowningdeath(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd8c0 | Name: object_anim_defs_kneelreact | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x66-entry kneel-reaction anim descriptor run
// (PTR_s_Kneelreact table, slots at 0x803d9c90).
// ===========================================================================
void object_anim_defs_kneelreact(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803d9c90, 0x66);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_Kneelreact_803d9c90, 0x66,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd910 | Name: object_anim_defs_kneelreact_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the kneel-reaction anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_kneelreact_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_kneelreact(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd934 | Name: object_anim_defs_r110drcontwalk | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xb-entry R110 doctor-continue-walk anim descriptor run
// (PTR_s_r110_drcontwalk table, slots at 0x803da620).
// ===========================================================================
void object_anim_defs_r110drcontwalk(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803da620, 0xb);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_r110_drcontwalk_803da620, 0xb,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd984 | Name: object_anim_defs_r110drcontwalk_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the R110 doctor-continue-walk anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_r110drcontwalk_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_r110drcontwalk(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bd9a8 | Name: object_anim_defs_flightcycle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 7-entry flight-cycle anim descriptor run
// (PTR_s_flight_cycle table, slots at 0x803da728; shares the ROM label with the
// batch_565 flight table at 0x8043eec0 but is a distinct address/run).
// ===========================================================================
void object_anim_defs_flightcycle(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803da728, 7);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flight_cycle_803da728, 7,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bd9f8 | Name: object_anim_defs_flightcycle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the flight-cycle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_flightcycle_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flightcycle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bda1c | Name: object_anim_defs_ashcowerrun | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xd-entry ash-cower-run anim descriptor run
// (PTR_s_ashcowerrun table, slots at 0x803da7d0). Worker only in this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_ashcowerrun(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803da7d0, 0xd);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ashcowerrun_803da7d0, 0xd,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bda6c | Name: object_anim_defs_ashcowerrun_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the ash-cower-run anim descriptors (mode 1).
// Worker object_anim_defs_ashcowerrun (table 0x803da7d0) was decompiled in batch_575.
// ===========================================================================
void object_anim_defs_ashcowerrun_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ashcowerrun(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bda90 | Name: object_anim_defs_flightcycle_803da908 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x1e-entry flight-cycle anim descriptor run
// (PTR_s_flight_cycle table, slots at 0x803da908; shares the ROM label with the
// batch_575 flight-cycle table at 0x803da728 but is a distinct address/run).
// ===========================================================================
void object_anim_defs_flightcycle_803da908(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, int mode, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803da908, 0x1e);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flight_cycle_803da908, 0x1e,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdae0 | Name: object_anim_defs_flightcycle_803da908_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the 0x1e-entry flight-cycle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_flightcycle_803da908_load(unsigned long long param_1, double param_2, double param_3,
                                                unsigned long long param_4, unsigned long long param_5,
                                                unsigned long long param_6, unsigned long long param_7,
                                                unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flightcycle_803da908(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                          1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdb04 | Name: object_anim_defs_ashcowerrun_803dabd8 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 1-entry ash-cower-run anim descriptor run
// (PTR_s_ashcowerrun table, slots at 0x803dabd8; shares the ROM label with the
// batch_575 ash-cower-run table at 0x803da7d0 but is a distinct address/run).
// ===========================================================================
void object_anim_defs_ashcowerrun_803dabd8(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, int mode, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dabd8, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ashcowerrun_803dabd8, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdb54 | Name: object_anim_defs_ashcowerrun_803dabd8_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the 1-entry ash-cower-run anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_ashcowerrun_803dabd8_load(unsigned long long param_1, double param_2, double param_3,
                                                unsigned long long param_4, unsigned long long param_5,
                                                unsigned long long param_6, unsigned long long param_7,
                                                unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ashcowerrun_803dabd8(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                          1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdb78 | Name: object_anim_defs_mattackheadbutt | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry monkey-attack head-butt anim descriptor run
// (PTR_s_mattackheadbutt table, slots at 0x803dabf0).
// ===========================================================================
void object_anim_defs_mattackheadbutt(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dabf0, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_mattackheadbutt_803dabf0, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdbc8 | Name: object_anim_defs_mattackheadbutt_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the monkey-attack head-butt anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_mattackheadbutt_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_mattackheadbutt(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdbec | Name: object_anim_defs_monkeyiceskit | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 2-entry monkey-ice-skit anim descriptor run
// (PTR_s_monkeyiceskit table, slots at 0x803dad70).
// ===========================================================================
void object_anim_defs_monkeyiceskit(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dad70, 2);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_monkeyiceskit_803dad70, 2,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdc3c | Name: object_anim_defs_monkeyiceskit_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the monkey-ice-skit anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_monkeyiceskit_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_monkeyiceskit(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdc60 | Name: object_anim_defs_girlroachattack | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 7-entry girl-roach-attack anim descriptor run
// (PTR_s_girlroachattack table, slots at 0x803dada0). Worker only in this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_girlroachattack(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dada0, 7);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_girlroachattack_803dada0, 7,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdcb0 | Name: object_anim_defs_girlroachattack_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the girl-roach-attack anim descriptors (mode 1).
// Worker object_anim_defs_girlroachattack (table 0x803dada0) was decompiled in batch_576.
// ===========================================================================
void object_anim_defs_girlroachattack_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_girlroachattack(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdcd4 | Name: object_anim_defs_magchargeidle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x14-entry mag-charge idle anim descriptor run
// (PTR_s_magcharge_idle table, slots at 0x803dae48).
// ===========================================================================
void object_anim_defs_magchargeidle(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dae48, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_magcharge_idle_803dae48, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdd24 | Name: object_anim_defs_magchargeidle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the mag-charge idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_magchargeidle_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_magchargeidle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdd48 | Name: object_anim_defs_devgirlheadshake | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xe-entry dev-girl head-shake anim descriptor run
// (PTR_s_devgirlheadshake table, slots at 0x803db028).
// ===========================================================================
void object_anim_defs_devgirlheadshake(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, int mode, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db028, 0xe);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_devgirlheadshake_803db028, 0xe,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bdd98 | Name: object_anim_defs_devgirlheadshake_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the dev-girl head-shake anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_devgirlheadshake_load(unsigned long long param_1, double param_2, double param_3,
                                            unsigned long long param_4, unsigned long long param_5,
                                            unsigned long long param_6, unsigned long long param_7,
                                            unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_devgirlheadshake(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                      1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bddbc | Name: object_anim_defs_jbeverybodyout | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry JB-everybody-out anim descriptor run
// (PTR_s_jbeverybodyout table, slots at 0x803db178).
// ===========================================================================
void object_anim_defs_jbeverybodyout(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db178, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jbeverybodyout_803db178, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bde0c | Name: object_anim_defs_jbeverybodyout_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the JB-everybody-out anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_jbeverybodyout_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jbeverybodyout(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bde30 | Name: object_anim_defs_uplinkidle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x2c-entry uplink-idle anim descriptor run
// (PTR_s_uplinkidle table, slots at 0x803db328).
// ===========================================================================
void object_anim_defs_uplinkidle(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db328, 0x2c);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_uplinkidle_803db328, 0x2c,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bde80 | Name: object_anim_defs_uplinkidle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (thunk): load the uplink-idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_uplinkidle_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_uplinkidle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bdea4 | Name: object_anim_defs_zomgroundspawn | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 1-entry zombie-ground-spawn anim descriptor run
// (PTR_s_zomgroundspawn table, slots at 0x803db748). Worker only in this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_zomgroundspawn(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db748, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomgroundspawn_803db748, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_578 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801bdef4 - 0x801be0e8. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_zomgroundspawn_803db748[];   // batch_577 worker's name table (thunked here)
extern char PTR_s_startflyby_803db760[];
extern char PTR_s_gren_back_todive_803db9e8[];
extern char PTR_s_ashwindowfall_803dbad8[];
extern char PTR_s_ashcower_803dbc58[];
extern char PTR_s_jbattackwalk360_803dc018[];
} // extern "C"

// ===========================================================================
// Address: 0x801bdef4 | Name: object_anim_defs_zomgroundspawn_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the zombie-ground-spawn anim descriptors
// (object_anim_defs_zomgroundspawn mode 1). Worker defined in batch_577 (0x801bdea4).
// ===========================================================================
void object_anim_defs_zomgroundspawn_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zomgroundspawn(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bdf18 | Name: object_anim_defs_startflyby | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x1b-entry start-fly-by (intro camera flythrough) anim
// descriptor run (PTR_s_startflyby table, slots at 0x803db760).
// ===========================================================================
void object_anim_defs_startflyby(unsigned long long param_1, double param_2, double param_3,
                                 unsigned long long param_4, unsigned long long param_5,
                                 unsigned long long param_6, unsigned long long param_7,
                                 unsigned long long param_8, int mode, unsigned int param_10,
                                 unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                 unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db760, 0x1b);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_startflyby_803db760, 0x1b,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bdf68 | Name: object_anim_defs_startflyby_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the start-fly-by anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_startflyby_load(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_startflyby(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801bdf8c | Name: object_anim_defs_gren_back_todive | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xa-entry grenade-back-to-dive anim descriptor run
// (PTR_s_gren_back_todive table, slots at 0x803db9e8).
// ===========================================================================
void object_anim_defs_gren_back_todive(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, int mode, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803db9e8, 10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_gren_back_todive_803db9e8, 10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801bdfdc | Name: object_anim_defs_gren_back_todive_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the grenade-back-to-dive anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_gren_back_todive_load(unsigned long long param_1, double param_2, double param_3,
                                            unsigned long long param_4, unsigned long long param_5,
                                            unsigned long long param_6, unsigned long long param_7,
                                            unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_gren_back_todive(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                      1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be000 | Name: object_anim_defs_ashwindowfall | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry ash-window-fall (scripted death) anim
// descriptor run (PTR_s_ashwindowfall table, slots at 0x803dbad8).
// ===========================================================================
void object_anim_defs_ashwindowfall(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dbad8, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ashwindowfall_803dbad8, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be050 | Name: object_anim_defs_ashwindowfall_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the ash-window-fall anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_ashwindowfall_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ashwindowfall(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be074 | Name: object_anim_defs_ashcower | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x28-entry ash-cower (scripted cowering) anim descriptor
// run (PTR_s_ashcower table, slots at 0x803dbc58).
// ===========================================================================
void object_anim_defs_ashcower(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dbc58, 0x28);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ashcower_803dbc58, 0x28,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be0c4 | Name: object_anim_defs_ashcower_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the ash-cower anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_ashcower_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ashcower(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be0e8 | Name: object_anim_defs_jbattackwalk360 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x1e-entry jb-attack-walk-360 anim descriptor run
// (PTR_s_jbattackwalk360 table, slots at 0x803dc018). Worker only in this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_jbattackwalk360(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc018, 0x1e);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jbattackwalk360_803dc018, 0x1e,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_579 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801be138 - 0x801be32c. Same load/release pattern: mode 0
// releases a contiguous descriptor-slot run (anim_release_descriptors), any
// other mode loads it from the shared ts3 anim-data folder via
// anim_load_descriptor_list keyed by a per-class name table. The small "_load"
// thunks are the public entry points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_ashbinocularidle_803dc2e8[];
extern char PTR_s_zomattack01_803dc3d8[];
extern char PTR_s_croweidle_803dc5b8[];
extern char PTR_s_jbcampullout_803dc7f8[];
extern char PTR_s_ghostgun_idle1_803dc990[];
} // extern "C"

// ===========================================================================
// Address: 0x801be138 | Name: object_anim_defs_jbattackwalk360_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the jb-attack-walk-360 anim descriptors
// (object_anim_defs_jbattackwalk360 mode 1). Worker defined in batch_578 (0x801be0e8).
// ===========================================================================
void object_anim_defs_jbattackwalk360_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jbattackwalk360(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be15c | Name: object_anim_defs_ashbinocularidle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0xa-entry ash-binocular-idle (scripted binocular pose)
// anim descriptor run (PTR_s_ashbinocularidle table, slots at 0x803dc2e8).
// ===========================================================================
void object_anim_defs_ashbinocularidle(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, int mode, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc2e8, 10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ashbinocularidle_803dc2e8, 10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be1ac | Name: object_anim_defs_ashbinocularidle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the ash-binocular-idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_ashbinocularidle_load(unsigned long long param_1, double param_2, double param_3,
                                            unsigned long long param_4, unsigned long long param_5,
                                            unsigned long long param_6, unsigned long long param_7,
                                            unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                            unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                            unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ashbinocularidle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                      1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be1d0 | Name: object_anim_defs_zomattack01 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x14-entry zombie-attack-01 anim descriptor run
// (PTR_s_zomattack01 table, slots at 0x803dc3d8).
// ===========================================================================
void object_anim_defs_zomattack01(unsigned long long param_1, double param_2, double param_3,
                                  unsigned long long param_4, unsigned long long param_5,
                                  unsigned long long param_6, unsigned long long param_7,
                                  unsigned long long param_8, int mode, unsigned int param_10,
                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc3d8, 0x14);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomattack01_803dc3d8, 0x14,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be220 | Name: object_anim_defs_zomattack01_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the zombie-attack-01 anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_zomattack01_load(unsigned long long param_1, double param_2, double param_3,
                                       unsigned long long param_4, unsigned long long param_5,
                                       unsigned long long param_6, unsigned long long param_7,
                                       unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                       unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                       unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zomattack01(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                 1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be244 | Name: object_anim_defs_croweidle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x18-entry crowe-idle (Crowe character idle) anim
// descriptor run (PTR_s_croweidle table, slots at 0x803dc5b8).
// ===========================================================================
void object_anim_defs_croweidle(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc5b8, 0x18);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_croweidle_803dc5b8, 0x18,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be294 | Name: object_anim_defs_croweidle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the crowe-idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_croweidle_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_croweidle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be2b8 | Name: object_anim_defs_jbcampullout | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x11-entry jb-cam-pullout (scripted camera pull-out) anim
// descriptor run (PTR_s_jbcampullout table, slots at 0x803dc7f8).
// ===========================================================================
void object_anim_defs_jbcampullout(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc7f8, 0x11);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_jbcampullout_803dc7f8, 0x11,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be308 | Name: object_anim_defs_jbcampullout_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the jb-cam-pullout anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_jbcampullout_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_jbcampullout(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be32c | Name: object_anim_defs_ghostgun_idle1 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry ghost-gun idle anim descriptor run
// (PTR_s_ghostgun_idle1 table, slots at 0x803dc990). Worker only this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_ghostgun_idle1(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dc990, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_ghostgun_idle1_803dc990, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_580 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801be37c - 0x801be570. Same load/release pattern as batches
// 562-579: mode 0 releases a contiguous descriptor-slot run, any other mode
// loads it from the shared ts3 anim-data folder via anim_load_descriptor_list
// keyed by a per-class name table. Small "_load" thunks are public entry
// points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_periscopetapsoldier_803dcb40[];
extern char PTR_s_zomcartattack_803dcd08[];
extern char PTR_s_9mmauto_shot_803dd098[];
extern char PTR_s_hatchopen_803dd560[];
extern char PTR_s_flamethrow_idle_803dd590[];
} // extern "C"

// ===========================================================================
// Address: 0x801be37c | Name: object_anim_defs_ghostgun_idle1_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the ghost-gun idle-1 anim descriptors
// (object_anim_defs_ghostgun_idle1 mode 1). Worker defined in batch_579 (0x801be32c).
// ===========================================================================
void object_anim_defs_ghostgun_idle1_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_ghostgun_idle1(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be3a0 | Name: object_anim_defs_periscopetapsoldier | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x13-entry periscope-tap-soldier (scripted periscope tap
// pose) anim descriptor run (PTR_s_periscopetapsoldier table, slots 0x803dcb40).
// ===========================================================================
void object_anim_defs_periscopetapsoldier(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, int mode, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dcb40, 0x13);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_periscopetapsoldier_803dcb40, 0x13,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be3f0 | Name: object_anim_defs_periscopetapsoldier_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the periscope-tap-soldier anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_periscopetapsoldier_load(unsigned long long param_1, double param_2, double param_3,
                                               unsigned long long param_4, unsigned long long param_5,
                                               unsigned long long param_6, unsigned long long param_7,
                                               unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_periscopetapsoldier(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                         1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be414 | Name: object_anim_defs_zomcartattack | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x26-entry zombie-cart-attack anim descriptor run
// (PTR_s_zomcartattack table, slots 0x803dcd08).
// ===========================================================================
void object_anim_defs_zomcartattack(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, int mode, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dcd08, 0x26);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_zomcartattack_803dcd08, 0x26,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be464 | Name: object_anim_defs_zomcartattack_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the zombie-cart-attack anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_zomcartattack_load(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_zomcartattack(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                   1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be488 | Name: object_anim_defs_9mmauto_shot | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x33-entry 9mm-auto shot anim descriptor run
// (PTR_s_9mmauto_shot table, slots 0x803dd098). The largest run in this batch.
// ===========================================================================
void object_anim_defs_9mmauto_shot(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dd098, 0x33);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_9mmauto_shot_803dd098, 0x33,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be4d8 | Name: object_anim_defs_9mmauto_shot_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the 9mm-auto shot anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_9mmauto_shot_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_9mmauto_shot(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be4fc | Name: object_anim_defs_hatchopen | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 2-entry hatch-open (vehicle/turret hatch) anim descriptor
// run (PTR_s_hatchopen table, slots 0x803dd560).
// ===========================================================================
void object_anim_defs_hatchopen(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dd560, 2);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_hatchopen_803dd560, 2,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be54c | Name: object_anim_defs_hatchopen_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the hatch-open anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_hatchopen_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_hatchopen(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be570 | Name: object_anim_defs_flamethrow_idle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x12-entry flamethrower-idle anim descriptor run
// (PTR_s_flamethrow_idle table, slots 0x803dd590). Worker only this batch; its
// "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_flamethrow_idle(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8, int mode, unsigned int param_10,
                                      unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                      unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dd590, 0x12);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flamethrow_idle_803dd590, 0x12,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// batch_581 : continuation of the per-weapon-class anim-descriptor wrappers.
// Address range 0x801be5c0 - 0x801be7b4. Same load/release pattern as batches
// 562-580: mode 0 releases a contiguous descriptor-slot run, any other mode
// loads it from the shared ts3 anim-data folder via anim_load_descriptor_list
// keyed by a per-class name table. Small "_load" thunks are public entry
// points (call the worker with mode=1).
// ---------------------------------------------------------------------------
extern "C" {
extern char PTR_s_nemosmg_32shot_803dd740[];
extern char PTR_s_botshock_803ddcb0[];
extern char PTR_s_dartgun_idle_803ddea8[];
extern char PTR_s_bomb_idle_803de028[];
extern char PTR_s_millsgrenade_idle_803de1a8[];
} // extern "C"

// ===========================================================================
// Address: 0x801be5c0 | Name: object_anim_defs_flamethrow_idle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the flamethrower-idle anim descriptors
// (object_anim_defs_flamethrow_idle mode 1). Worker defined in batch_580 (0x801be570).
// ===========================================================================
void object_anim_defs_flamethrow_idle_load(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flamethrow_idle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                     1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be5e4 | Name: object_anim_defs_nemosmg_32shot | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x3a-entry nemo-smg 32-shot anim descriptor run
// (PTR_s_nemosmg_32shot table, slots 0x803dd740). The largest run in this batch.
// ===========================================================================
void object_anim_defs_nemosmg_32shot(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, int mode, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803dd740, 0x3a);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_nemosmg_32shot_803dd740, 0x3a,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be634 | Name: object_anim_defs_nemosmg_32shot_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the nemo-smg 32-shot anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_nemosmg_32shot_load(unsigned long long param_1, double param_2, double param_3,
                                          unsigned long long param_4, unsigned long long param_5,
                                          unsigned long long param_6, unsigned long long param_7,
                                          unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                          unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                          unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_nemosmg_32shot(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                    1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be658 | Name: object_anim_defs_botshock | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x15-entry bot-shock (robot electrocution reaction) anim
// descriptor run (PTR_s_botshock table, slots 0x803ddcb0).
// ===========================================================================
void object_anim_defs_botshock(unsigned long long param_1, double param_2, double param_3,
                               unsigned long long param_4, unsigned long long param_5,
                               unsigned long long param_6, unsigned long long param_7,
                               unsigned long long param_8, int mode, unsigned int param_10,
                               unsigned int param_11, unsigned int param_12, unsigned int param_13,
                               unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803ddcb0, 0x15);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_botshock_803ddcb0, 0x15,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be6a8 | Name: object_anim_defs_botshock_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the bot-shock anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_botshock_load(unsigned long long param_1, double param_2, double param_3,
                                    unsigned long long param_4, unsigned long long param_5,
                                    unsigned long long param_6, unsigned long long param_7,
                                    unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                    unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                    unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_botshock(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                              1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be6cc | Name: object_anim_defs_dartgun_idle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry dart-gun idle anim descriptor run
// (PTR_s_dartgun_idle table, slots 0x803ddea8).
// ===========================================================================
void object_anim_defs_dartgun_idle(unsigned long long param_1, double param_2, double param_3,
                                   unsigned long long param_4, unsigned long long param_5,
                                   unsigned long long param_6, unsigned long long param_7,
                                   unsigned long long param_8, int mode, unsigned int param_10,
                                   unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                   unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803ddea8, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_dartgun_idle_803ddea8, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be71c | Name: object_anim_defs_dartgun_idle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the dart-gun idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_dartgun_idle_load(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_dartgun_idle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                  1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be740 | Name: object_anim_defs_bomb_idle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry bomb idle anim descriptor run
// (PTR_s_bomb_idle table, slots 0x803de028).
// ===========================================================================
void object_anim_defs_bomb_idle(unsigned long long param_1, double param_2, double param_3,
                                unsigned long long param_4, unsigned long long param_5,
                                unsigned long long param_6, unsigned long long param_7,
                                unsigned long long param_8, int mode, unsigned int param_10,
                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803de028, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_bomb_idle_803de028, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}

// ===========================================================================
// Address: 0x801be790 | Name: object_anim_defs_bomb_idle_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry: load the bomb idle anim descriptors (mode 1).
// ===========================================================================
void object_anim_defs_bomb_idle_load(unsigned long long param_1, double param_2, double param_3,
                                     unsigned long long param_4, unsigned long long param_5,
                                     unsigned long long param_6, unsigned long long param_7,
                                     unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                     unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                     unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_bomb_idle(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                               1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}

// ===========================================================================
// Address: 0x801be7b4 | Name: object_anim_defs_millsgrenade_idle | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x10-entry mills-grenade idle anim descriptor run
// (PTR_s_millsgrenade_idle table, slots 0x803de1a8). Worker only this batch;
// its "_load" thunk is captured by a later batch.
// ===========================================================================
void object_anim_defs_millsgrenade_idle(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, int mode, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803de1a8, 0x10);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_millsgrenade_idle_803de1a8, 0x10,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// batch_582 : 0x801be804 - 0x801bfbe8
// Continuation of the object-weapon anim-descriptor module. This batch closes
// out the remaining per-class load/release wrapper pairs, plus three larger
// pieces: the global weapon/prop bind-pose descriptor table loader
// (object_anim_defs_bindpose_table, 0x801be89c), the master anim-pack
// registration driver (object_anim_defs_register_all_packs, 0x801bf3ac) which
// opens every per-class pack by name and hands it to its "_load" thunk, and a
// small state-block zero-initializer (object_anim_defs_reset_state, 0x801bfbe8).
//
// Two new ROM helpers appear here:
//   anim_load_descriptor_slot (0x???): chained per-slot descriptor loader used
//     by the bindpose table. It takes (ctx, <FP ABI>, objType, slotId,
//     nameFolder, count, guardFlag, ...) and returns the next chained context
//     (register-pair ull whose value is threaded into the next call). Called
//     through a typed pointer so the wide 16-arg ABI + the returned context are
//     preserved.
//   pack_find_by_name (0x???): looks up an anim pack; the ROM ignores the C
//     prototype's single arg and returns a register pair (hi = pack index or
//     -1, lo = pack id). Threaded through a typed 0-arg pointer so both halves
//     survive.
// ===========================================================================
extern "C" {
// Worker descriptor name tables for this batch's load/release wrappers.
extern char PTR_s_flight_cycle_803de328[];      // flight-cycle variant 2 (1 slot)
extern char PTR_s_cortezpovidle_cam_803de340[]; // cortez POV idle camera (0x6e slots)
extern char PTR_s_r110_run_803ded90[];          // r110 run variant 2 (0x23 slots)
// Guard flag written 1 by the bindpose table loader (its per-slot arg too).
extern int DAT_806125e0;
// --- bind-pose descriptor name strings (object_anim_defs_bindpose_table) ---
extern char s_anim_data_ts3_human_19_bindpose__8044cc6c[];
extern char s_anim_data_ts3_human_talking_bind_8044cc90[];
extern char s_anim_data_ts3_gun_bindpose_raw_8044ccbc[];
extern char s_anim_data_ts3_luger_arm_new_bind_8044ccdc[];
extern char s_anim_data_ts3_arms_bindpose_raw_8044cd04[];
extern char s_anim_data_ts3_cortfirspershandno_8044cd24[];
extern char s_anim_data_ts3_byplanesegbindpose_8044cd5c[];
extern char s_anim_data_ts3_csmaindoorblast_b__8044cd84[];
extern char s_anim_data_ts3_junglecelldoors_b__8044cda8[];
extern char s_anim_data_ts3_castletankdoor_b_r_8044cdcc[];
extern char s_anim_data_ts3_mansionboss_scaled_8044cdf0[];
extern char s_anim_data_ts3_ragbug_texboned_sc_8044ce20[];
extern char s_anim_data_ts3_giantrobot_texbone_8044ce4c[];
extern char s_anim_data_ts3_sentrydroid_texbon_8044ce7c[];
extern char s_anim_data_ts3_medicaldroid_boned_8044cea8[];
extern char s_anim_data_ts3_tankcrowingame_bon_8044ced0[];
extern char s_anim_data_ts3_housecolapsebindpo_8044cefc[];
extern char s_anim_data_ts3_frontwallfallbindp_8044cf24[];
extern char s_anim_data_ts3_truckbindpose_raw_8044cf4c[];
extern char s_anim_data_ts3_jetpack_scaled_bon_8044cf6c[];
extern char s_anim_data_ts3_luger_b_raw_8044cf98[];
extern char s_anim_data_ts3_nemosmg_b_raw_8044cfb4[];
extern char s_anim_data_ts3_rifle_b_raw_8044cfd0[];
extern char s_anim_data_ts3_millsgrenade_b_raw_8044cfec[];
extern char s_anim_data_ts3_flaregun_b_raw_8044d010[];
extern char s_anim_data_ts3_bomb_b_raw_8044d030[];
extern char s_anim_data_ts3_uplinkhighpol_b_ra_8044d04c[];
extern char s_anim_data_ts3_Assaultrifle_b_raw_8044d070[];
extern char s_anim_data_ts3_machinepistol_b_ra_8044d094[];
extern char s_anim_data_ts3_pumpaction_b_raw_8044d0b8[];
extern char s_anim_data_ts3_future_shotgun_b_r_8044d0d8[];
extern char s_anim_data_ts3_9mmauto_b_raw_8044d0fc[];
extern char s_anim_data_ts3_futuresniper_bindp_8044d118[];
extern char s_anim_data_ts3_heatseeker_b_raw_8044d144[];
extern char s_anim_data_ts3_plasmaauto_b_raw_8044d164[];
extern char s_anim_data_ts3_homing_launcher_b__8044d184[];
extern char s_anim_data_ts3_doubleshotgun_b_ra_8044d1a8[];
extern char s_anim_data_ts3_baseballbat_b_raw_8044d1cc[];
extern char s_anim_data_ts3_ghostgun_b_raw_8044d1ec[];
extern char s_anim_data_ts3_flamethrower_b_raw_8044d20c[];
extern char s_anim_data_ts3_gun_magnum_b_raw_8044d230[];
extern char s_anim_data_ts3_plasma_pistol_b_ra_8044d250[];
extern char s_anim_data_ts3_minigun_b_raw_8044d274[];
extern char s_anim_data_ts3_electrotool_b_raw_8044d290[];
extern char s_anim_data_ts3_dartgun_offset_b_r_8044d2b0[];
extern char s_anim_data_ts3_magcharger_b_raw_8044d2d4[];
extern char s_anim_data_ts3_sbp500_b_raw_8044d2f4[];
extern char s_anim_data_ts3_harpoongun_b_raw_8044d310[];
extern char s_anim_data_ts3_remotemine_b_raw_8044d330[];
extern char s_anim_data_ts3_detonator_b_raw_8044d350[];
extern char s_anim_data_ts3_brick_boned_b_raw_8044d370[];
extern char s_anim_data_ts3_cortezwithmouth_te_8044d390[];
extern char s_anim_data_ts3_cat_texboned_b_raw_8044d3c0[];
extern char s_anim_data_ts3_khallos_ingamecs_s_8044d3e4[];
extern char s_anim_data_ts3_tipperingamecs_tex_8044d41c[];
extern char s_anim_data_ts3_key_b_raw_8044d450[];
extern char s_anim_data_ts3_armourcrane_b_raw_8044d468[];
extern char s_anim_data_ts3_chandelier_b_raw_8044d488[];
extern char s_anim_data_ts3_zombiewall_b_raw_8044d4a8[];
extern char s_anim_data_ts3_moosepanel_b_raw_8044d4c8[];
extern char s_anim_data_ts3_rope_b_raw_8044d4e8[];
extern char s_anim_data_ts3_first_cables_b_raw_8044d504[];
extern char s_anim_data_ts3_minecart_b_raw_8044d528[];
extern char s_anim_data_ts3_torturerack_b_raw_8044d548[];
extern char s_anim_data_ts3_mansiontable_b_raw_8044d568[];
extern char s_anim_data_ts3_rappelrope_b_raw_8044d58c[];
extern char s_anim_data_ts3_pincerblade_b_raw_8044d5ac[];
extern char s_anim_data_ts3_courtyard_porchfra_8044d5cc[];
extern char s_anim_data_ts3_worm_texboned_scal_8044d5f8[];
extern char s_anim_data_ts3_wraith_b_raw_8044d624[];
extern char s_anim_data_ts3_robotarm_tschamber_8044d640[];
extern char s_anim_data_ts3_exploding_chamber__8044d668[];
extern char s_anim_data_ts3_bridge_collapse_b__8044d694[];
extern char s_anim_data_ts3_cleaningrobot_texb_8044d6b8[];
extern char s_anim_data_ts3_minibindpose2_raw_8044d6ec[];
extern char s_anim_data_ts3_newrailbot_texbone_8044d70c[];
extern char s_anim_data_ts3_newrailspider_texb_8044d734[];
extern char s_anim_data_ts3_strapflap_b_raw_8044d760[];
extern char s_anim_data_ts3_smallflyer_boned_s_8044d780[];
extern char s_anim_data_ts3_beamengine1a_b_raw_8044d7ac[];
extern char s_anim_data_ts3_beamengine1amirror_8044d7d0[];
extern char s_anim_data_ts3_nem_wareho_metalcr_8044d7f8[];
extern char s_anim_data_ts3_walkercrowingame_t_8044d824[];
extern char s_anim_data_ts3_anchor_b_raw_8044d85c[];
extern char s_anim_data_ts3_banana_prop2_b_raw_8044d878[];
extern char s_anim_data_ts3_barrel_b_raw_8044d89c[];
extern char s_anim_data_ts3_bat_b_raw_8044d8b8[];
extern char s_anim_data_ts3_bigsword_b_raw_8044d8d0[];
extern char s_anim_data_ts3_blister_b_raw_8044d8f0[];
extern char s_anim_data_ts3_blistermonkey_b_ra_8044d90c[];
extern char s_anim_data_ts3_board_b_raw_8044d930[];
extern char s_anim_data_ts3_cat_b_raw_8044d94c[];
extern char s_anim_data_ts3_coffin_b_raw_8044d964[];
extern char s_anim_data_ts3_foot_b_raw_8044d980[];
extern char s_anim_data_ts3_guardpis_b_raw_8044d99c[];
extern char s_anim_data_ts3_laserpistol_b_raw_8044d9bc[];
extern char s_anim_data_ts3_fe_magnum_b_raw_8044d9dc[];
extern char s_anim_data_ts3_nemosmg_prop_b_raw_8044d9fc[];
extern char s_anim_data_ts3_plasmagun_b_raw_8044da20[];
extern char s_anim_data_ts3_shotgun_b_raw_8044da40[];
extern char s_anim_data_ts3_small_sword_b_raw_8044da5c[];
extern char s_anim_data_ts3_standee_b_raw_8044da7c[];
extern char s_anim_data_ts3_sword_b_raw_8044da98[];
extern char s_anim_data_ts3_timedevice_b_raw_8044dab4[];
extern char s_anim_data_ts3_zomhead_b_raw_8044dad4[];
} // extern "C"

// anim_load_descriptor_slot: chained per-slot bind-pose loader (wide 16-arg
// FP-context PPC ABI). It returns the next chained context in a register that
// is fed forward into the following call. Route through a typed pointer to its
// ROM address so both the wide arg list and the returned context are preserved.
typedef unsigned long long (*AnimLoadSlotFn)(unsigned long long ctx, double, double,
                                             unsigned long long, unsigned long long,
                                             unsigned long long, unsigned long long,
                                             unsigned long long, int objType, int slotId,
                                             const char *nameFolder, int count, int guardFlag,
                                             unsigned int, unsigned int, unsigned int);
static const AnimLoadSlotFn __ow_anim_load_descriptor_slot = (AnimLoadSlotFn)0x801fd0f4;

// pack_find_by_name: the ROM ignores the C prototype's arg and returns a
// register pair {hi = index/-1, lo = id}. Route through a 0-arg typed pointer.
static inline unsigned long long __ow_pack_find_by_name(void)
{
    return ((unsigned long long (*)(void))0x8038b0e8)();
}

// ===========================================================================
// Address: 0x801be828 | Name: object_anim_defs_flightcycle_803de328 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the single-slot flight-cycle anim descriptor
// (PTR_s_flight_cycle table variant, slots 0x803de328). Distinct-address twin
// of the earlier flight-cycle tables; suffixed by its slot-array address.
// ===========================================================================
void object_anim_defs_flightcycle_803de328(unsigned long long param_1, double param_2, double param_3,
                                           unsigned long long param_4, unsigned long long param_5,
                                           unsigned long long param_6, unsigned long long param_7,
                                           unsigned long long param_8, int mode, unsigned int param_10,
                                           unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                           unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803de328, 1);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_flight_cycle_803de328, 1,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801be804 | Name: object_anim_defs_flightcycle_803de328_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (mode 1) for object_anim_defs_flightcycle_803de328. Defined
// after its worker because the ROM lays the thunk out before the worker.
// ===========================================================================
void object_anim_defs_flightcycle_803de328_load(unsigned long long param_1, double param_2, double param_3,
                                                unsigned long long param_4, unsigned long long param_5,
                                                unsigned long long param_6, unsigned long long param_7,
                                                unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                                unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                                unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flightcycle_803de328(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                          1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801be878 | Name: object_anim_defs_flightcycle_803de328_load_b | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Second thunk to the same worker (a distinct ROM entry that also loads mode 1).
// ===========================================================================
void object_anim_defs_flightcycle_803de328_load_b(unsigned long long param_1, double param_2, double param_3,
                                                  unsigned long long param_4, unsigned long long param_5,
                                                  unsigned long long param_6, unsigned long long param_7,
                                                  unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                                  unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                                  unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_flightcycle_803de328(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                          1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801be89c | Name: object_anim_defs_bindpose_table | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Global weapon/prop bind-pose descriptor table loader. Registers ~110
// bind-pose anim descriptors in one pass: for each entry it calls
// anim_load_descriptor_slot(objType, slotId, nameFolder, count) chained through
// the returned context, keyed by a per-model .raw/bindpose anim data name.
// Slot ids run 0x23.. and 0x985.. (props). Sets the DAT_806125e0 guard to 1 on
// completion so the table is only built once. The wide FP-context PPC ABI tail
// (param_2..param_8, param_14..param_16) is carried verbatim.
// ===========================================================================
void object_anim_defs_bindpose_table(unsigned long long param_1, double param_2, double param_3,
                                      unsigned long long param_4, unsigned long long param_5,
                                      unsigned long long param_6, unsigned long long param_7,
                                      unsigned long long param_8)
{
    unsigned int r8 = 0, r9 = 0, r10 = 0;
    unsigned long long ctx = param_1;
    // Each row: (objType, slotId, nameFolder, descriptorCount).
    struct { int objType; int slotId; const char *name; int count; } rows[] = {
        {0x00, 0x23, s_anim_data_ts3_human_19_bindpose__8044cc6c, 0x13},
        {0x7c, 0x24, s_anim_data_ts3_human_talking_bind_8044cc90, 0x14},
        {0x05, 0x9a, s_anim_data_ts3_gun_bindpose_raw_8044ccbc, 0x09},
        {0x06, 0x9b, s_anim_data_ts3_luger_arm_new_bind_8044ccdc, 0x13},
        {0x06, 0x9c, s_anim_data_ts3_luger_arm_new_bind_8044ccdc, 0x13},
        {0x06, 0x9d, s_anim_data_ts3_arms_bindpose_raw_8044cd04, 0x13},
        {0x24, 0x9e, s_anim_data_ts3_cortfirspershandno_8044cd24, 0x14},
        {0x0d, 0x9f, s_anim_data_ts3_byplanesegbindpose_8044cd5c, 0x12},
        {0x23, 0xa0, s_anim_data_ts3_csmaindoorblast_b__8044cd84, 0x0b},
        {0x26, 0xa1, s_anim_data_ts3_junglecelldoors_b__8044cda8, 0x10},
        {0x22, 0xa2, s_anim_data_ts3_castletankdoor_b_r_8044cdcc, 0x11},
        {0x45, 0xa3, s_anim_data_ts3_mansionboss_scaled_8044cdf0, 0x1e},
        {0x80, 0xa4, s_anim_data_ts3_ragbug_texboned_sc_8044ce20, 0x06},
        {0x53, 0xa5, s_anim_data_ts3_giantrobot_texbone_8044ce4c, 0x12},
        {0x4c, 0xa6, s_anim_data_ts3_sentrydroid_texbon_8044ce7c, 0x11},
        {0x4b, 0xa7, s_anim_data_ts3_medicaldroid_boned_8044cea8, 0x0f},
        {0x4d, 0xa8, s_anim_data_ts3_tankcrowingame_bon_8044ced0, 0x22},
        {0x10, 0xa9, s_anim_data_ts3_housecolapsebindpo_8044cefc, 0x12},
        {0x11, 0xaa, s_anim_data_ts3_frontwallfallbindp_8044cf24, 0x06},
        {0x12, 0xab, s_anim_data_ts3_truckbindpose_raw_8044cf4c, 0x09},
        {0x2e, 0xac, s_anim_data_ts3_jetpack_scaled_bon_8044cf6c, 0x14},
        {0x07, 0xad, s_anim_data_ts3_luger_b_raw_8044cf98, 0x08},
        {0x25, 0xae, s_anim_data_ts3_nemosmg_b_raw_8044cfb4, 0x0a},
        {0x0c, 0xaf, s_anim_data_ts3_rifle_b_raw_8044cfd0, 0x0c},
        {0x0b, 0xb0, s_anim_data_ts3_millsgrenade_b_raw_8044cfec, 0x07},
        {0x29, 0xb1, s_anim_data_ts3_flaregun_b_raw_8044d010, 0x08},
        {0x2a, 0xb2, s_anim_data_ts3_bomb_b_raw_8044d030, 0x05},
        {0x2b, 0xb3, s_anim_data_ts3_uplinkhighpol_b_ra_8044d04c, 0x12},
        {0x2f, 0xb4, s_anim_data_ts3_Assaultrifle_b_raw_8044d070, 0x06},
        {0x30, 0xb5, s_anim_data_ts3_machinepistol_b_ra_8044d094, 0x05},
        {0x31, 0xb6, s_anim_data_ts3_pumpaction_b_raw_8044d0b8, 0x09},
        {0x5c, 0xb7, s_anim_data_ts3_future_shotgun_b_r_8044d0d8, 0x07},
        {0x32, 0xb8, s_anim_data_ts3_9mmauto_b_raw_8044d0fc, 0x08},
        {0x36, 0xb9, s_anim_data_ts3_futuresniper_bindp_8044d118, 0x08},
        {0x37, 0xba, s_anim_data_ts3_heatseeker_b_raw_8044d144, 0x07},
        {0x39, 0xbb, s_anim_data_ts3_plasmaauto_b_raw_8044d164, 0x0a},
        {0x3a, 0xbc, s_anim_data_ts3_homing_launcher_b__8044d184, 0x09},
        {0x3d, 0xbd, s_anim_data_ts3_doubleshotgun_b_ra_8044d1a8, 0x08},
        {0x3f, 0xbe, s_anim_data_ts3_baseballbat_b_raw_8044d1cc, 0x01},
        {0x42, 0xbf, s_anim_data_ts3_ghostgun_b_raw_8044d1ec, 0x0e},
        {0x43, 0xc0, s_anim_data_ts3_flamethrower_b_raw_8044d20c, 0x04},
        {0x44, 0xc1, s_anim_data_ts3_gun_magnum_b_raw_8044d230, 0x0c},
        {0x4f, 0xc2, s_anim_data_ts3_plasma_pistol_b_ra_8044d250, 0x07},
        {0x50, 0xc3, s_anim_data_ts3_minigun_b_raw_8044d274, 0x0d},
        {0x52, 0xc4, s_anim_data_ts3_electrotool_b_raw_8044d290, 0x06},
        {0x0f, 0xc5, s_anim_data_ts3_dartgun_offset_b_r_8044d2b0, 0x06},
        {0x5a, 0xc6, s_anim_data_ts3_magcharger_b_raw_8044d2d4, 0x0d},
        {0x5b, 0xc7, s_anim_data_ts3_sbp500_b_raw_8044d2f4, 0x06},
        {0x79, 0xc8, s_anim_data_ts3_harpoongun_b_raw_8044d310, 0x0b},
        {0x7d, 0xc9, s_anim_data_ts3_remotemine_b_raw_8044d330, 0x02},
        {0x7e, 0xca, s_anim_data_ts3_detonator_b_raw_8044d350, 0x03},
        {0x7f, 0xcb, s_anim_data_ts3_brick_boned_b_raw_8044d370, 0x02},
        {0x27, 0xcc, s_anim_data_ts3_cortezwithmouth_te_8044d390, 0x19},
        {0x33, 0xcd, s_anim_data_ts3_cat_texboned_b_raw_8044d3c0, 0x04},
        {0x34, 0xce, s_anim_data_ts3_khallos_ingamecs_s_8044d3e4, 0x19},
        {0x35, 0xcf, s_anim_data_ts3_tipperingamecs_tex_8044d41c, 0x19},
        {0x28, 0xd0, s_anim_data_ts3_key_b_raw_8044d450, 0x01},
        {0x38, 0xd1, s_anim_data_ts3_armourcrane_b_raw_8044d468, 0x04},
        {0x3b, 0xd2, s_anim_data_ts3_chandelier_b_raw_8044d488, 0x09},
        {0x3c, 0xd3, s_anim_data_ts3_zombiewall_b_raw_8044d4a8, 0x07},
        {0x3e, 0xd4, s_anim_data_ts3_moosepanel_b_raw_8044d4c8, 0x10},
        {0x40, 0xd5, s_anim_data_ts3_rope_b_raw_8044d4e8, 0x08},
        {0x46, 0xd6, s_anim_data_ts3_first_cables_b_raw_8044d504, 0x07},
        {0x41, 0xd7, s_anim_data_ts3_minecart_b_raw_8044d528, 0x01},
        {0x4a, 0xd8, s_anim_data_ts3_torturerack_b_raw_8044d548, 0x08},
        {0x48, 0xd9, s_anim_data_ts3_mansiontable_b_raw_8044d568, 0x0f},
        {0x57, 0xda, s_anim_data_ts3_rappelrope_b_raw_8044d58c, 0x06},
        {0x59, 0xdb, s_anim_data_ts3_pincerblade_b_raw_8044d5ac, 0x0a},
        {0x7b, 0xdc, s_anim_data_ts3_courtyard_porchfra_8044d5cc, 0x0c},
        {0x47, 0xf0, s_anim_data_ts3_worm_texboned_scal_8044d5f8, 0x0a},
        {0x49, 0xf1, s_anim_data_ts3_wraith_b_raw_8044d624, 0x06},
        {0x4e, 0xf2, s_anim_data_ts3_robotarm_tschamber_8044d640, 0x03},
        {0x51, 0xf3, s_anim_data_ts3_exploding_chamber__8044d668, 0x15},
        {0x58, 0xf4, s_anim_data_ts3_bridge_collapse_b__8044d694, 0x10},
        {0x5e, 0xf5, s_anim_data_ts3_cleaningrobot_texb_8044d6b8, 0x05},
        {0x54, 0xf6, s_anim_data_ts3_minibindpose2_raw_8044d6ec, 0x05},
        {0x55, 0xf7, s_anim_data_ts3_newrailbot_texbone_8044d70c, 0x03},
        {0x56, 0xf8, s_anim_data_ts3_newrailspider_texb_8044d734, 0x0f},
        {0x5d, 0xf9, s_anim_data_ts3_strapflap_b_raw_8044d760, 0x0c},
        {0x5f, 0xfa, s_anim_data_ts3_smallflyer_boned_s_8044d780, 0x0c},
        {0x60, 0xfb, s_anim_data_ts3_beamengine1a_b_raw_8044d7ac, 0x07},
        {0x61, 0xfc, s_anim_data_ts3_beamengine1amirror_8044d7d0, 0x07},
        {0x78, 0xfd, s_anim_data_ts3_nem_wareho_metalcr_8044d7f8, 0x0a},
        {0x7a, 0xfe, s_anim_data_ts3_walkercrowingame_t_8044d824, 0x19},
        {0x62, 0x985, s_anim_data_ts3_anchor_b_raw_8044d85c, 0x01},
        {0x63, 0x986, s_anim_data_ts3_banana_prop2_b_raw_8044d878, 0x01},
        {0x64, 0x987, s_anim_data_ts3_barrel_b_raw_8044d89c, 0x01},
        {0x65, 0x988, s_anim_data_ts3_bat_b_raw_8044d8b8, 0x01},
        {0x66, 0x989, s_anim_data_ts3_bigsword_b_raw_8044d8d0, 0x01},
        {0x67, 0x98a, s_anim_data_ts3_blister_b_raw_8044d8f0, 0x01},
        {0x68, 0x98b, s_anim_data_ts3_blistermonkey_b_ra_8044d90c, 0x01},
        {0x69, 0x98c, s_anim_data_ts3_board_b_raw_8044d930, 0x01},
        {0x6b, 0x98d, s_anim_data_ts3_cat_b_raw_8044d94c, 0x01},
        {0x6a, 0x98e, s_anim_data_ts3_coffin_b_raw_8044d964, 0x01},
        {0x6c, 0x98f, s_anim_data_ts3_foot_b_raw_8044d980, 0x01},
        {0x6d, 0x990, s_anim_data_ts3_guardpis_b_raw_8044d99c, 0x01},
        {0x6e, 0x991, s_anim_data_ts3_laserpistol_b_raw_8044d9bc, 0x01},
        {0x6f, 0x992, s_anim_data_ts3_fe_magnum_b_raw_8044d9dc, 0x01},
        {0x70, 0x993, s_anim_data_ts3_nemosmg_prop_b_raw_8044d9fc, 0x01},
        {0x71, 0x994, s_anim_data_ts3_plasmagun_b_raw_8044da20, 0x01},
        {0x72, 0x995, s_anim_data_ts3_shotgun_b_raw_8044da40, 0x01},
        {0x73, 0x996, s_anim_data_ts3_small_sword_b_raw_8044da5c, 0x01},
        {0x74, 0x997, s_anim_data_ts3_standee_b_raw_8044da7c, 0x01},
        {0x75, 0x998, s_anim_data_ts3_sword_b_raw_8044da98, 0x01},
        {0x76, 0x999, s_anim_data_ts3_timedevice_b_raw_8044dab4, 0x01},
        {0x77, 0x99a, s_anim_data_ts3_zomhead_b_raw_8044dad4, 0x01},
    };
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); ++i) {
        ctx = __ow_anim_load_descriptor_slot(ctx, param_2, param_3, param_4, param_5, param_6, param_7,
                                             param_8, rows[i].objType, rows[i].slotId, rows[i].name,
                                             rows[i].count, DAT_806125e0, r8, r9, r10);
    }
    DAT_806125e0 = 1;
}
// ===========================================================================
// Address: 0x801bf2c4 | Name: object_anim_defs_cortezpovidle_cam | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x6e-entry cortez first-person-view idle camera anim
// descriptor run (PTR_s_cortezpovidle_cam table, slots 0x803de3c0-based).
// ===========================================================================
void object_anim_defs_cortezpovidle_cam(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, int mode, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803de340, 0x6e);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_cortezpovidle_cam_803de340, 0x6e,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bf314 | Name: object_anim_defs_cortezpovidle_cam_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (mode 1) for object_anim_defs_cortezpovidle_cam.
// ===========================================================================
void object_anim_defs_cortezpovidle_cam_load(unsigned long long param_1, double param_2, double param_3,
                                             unsigned long long param_4, unsigned long long param_5,
                                             unsigned long long param_6, unsigned long long param_7,
                                             unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_cortezpovidle_cam(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bf338 | Name: object_anim_defs_r110_run_803ded90 | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Load / release the 0x23-entry r110 run anim descriptor run (PTR_s_r110_run
// table variant, slots 0x803ded90). Distinct-address twin of the earlier
// r110-run table (object_anim_defs_run, batch_562).
// ===========================================================================
void object_anim_defs_r110_run_803ded90(unsigned long long param_1, double param_2, double param_3,
                                        unsigned long long param_4, unsigned long long param_5,
                                        unsigned long long param_6, unsigned long long param_7,
                                        unsigned long long param_8, int mode, unsigned int param_10,
                                        unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                        unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_10; (void)param_11;
    if (mode == 0) {
        __ow_anim_release_descriptors((int)0x803ded90, 0x23);
    } else {
        __ow_anim_load_descriptor_list(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       s_anim_data_ts3__80441448, PTR_s_r110_run_803ded90, 0x23,
                                       param_12, param_13, param_14, param_15, param_16);
    }
}
// ===========================================================================
// Address: 0x801bf388 | Name: object_anim_defs_r110_run_803ded90_load | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Public entry (mode 1) for object_anim_defs_r110_run_803ded90.
// ===========================================================================
void object_anim_defs_r110_run_803ded90_load(unsigned long long param_1, double param_2, double param_3,
                                             unsigned long long param_4, unsigned long long param_5,
                                             unsigned long long param_6, unsigned long long param_7,
                                             unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                             unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                             unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9;
    object_anim_defs_r110_run_803ded90(param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                                       1, param_10, param_11, param_12, param_13, param_14, param_15, param_16);
}
// ===========================================================================
// Address: 0x801bf3ac | Name: object_anim_defs_register_all_packs | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Master anim-pack registration driver. For each per-class anim pack it calls
// pack_find_by_name (which returns a {index, id} register pair); on a valid
// index (!= -1) it invokes that class's "_load" thunk with the resolved
// (index, id). The pack-name argument is baked into each pack_find_by_name call
// site in the ROM (the C prototype's single arg is ignored), so we thread the
// returned pair only. Covers every object_anim_defs_*_load thunk in this module
// including this batch's four new flight-cycle / r110-run / cortez thunks.
// Non-matching: the FP-context first arg (extraout_f1) is an ABI carry we pass
// through as param_1 unchanged.
// ===========================================================================
void object_anim_defs_register_all_packs(unsigned long long param_1, double param_2, double param_3,
                                         unsigned long long param_4, unsigned long long param_5,
                                         unsigned long long param_6, unsigned long long param_7,
                                         unsigned long long param_8, unsigned int param_9, unsigned int param_10,
                                         unsigned int param_11, unsigned int param_12, unsigned int param_13,
                                         unsigned int param_14, unsigned int param_15, unsigned int param_16)
{
    (void)param_9; (void)param_10;
    // Ordered list of per-class "_load" dispatchers, matching the ROM order.
    typedef void (*AnimDefsLoadFn)(unsigned long long, double, double, unsigned long long,
                                   unsigned long long, unsigned long long, unsigned long long,
                                   unsigned long long, unsigned int, unsigned int, unsigned int,
                                   unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    static const AnimDefsLoadFn kLoaders[] = {
        object_anim_defs_mingun_load, object_anim_defs_run_load, object_anim_defs_monkeydance_load,
        object_anim_defs_futuresg_load, object_anim_defs_zombie_load, object_anim_defs_basebat_load,
        object_anim_defs_tsbolt_load, object_anim_defs_mine_load, object_anim_defs_assault_load,
        object_anim_defs_pumpaction_load, object_anim_defs_plasma_load, object_anim_defs_drivetruck_load,
        object_anim_defs_monkeybeat_load, object_anim_defs_monkeydance2_load, object_anim_defs_leaderpeak_load,
        object_anim_defs_zsearch_load, object_anim_defs_flight_load, object_anim_defs_rebelmale_load,
        object_anim_defs_giantrob_load, object_anim_defs_peekbox_load, object_anim_defs_electro_load,
        object_anim_defs_zomdeath_load, object_anim_defs_gensoldier_load, object_anim_defs_monkeycell_load,
        object_anim_defs_flaregun_load, object_anim_defs_homing_load, object_anim_defs_gondola_load,
        object_anim_defs_rebelambient_load, object_anim_defs_maleconvers_load, object_anim_defs_femguard_load,
        object_anim_defs_sollookdials_load, object_anim_defs_drunkguardend_a_load, object_anim_defs_drunkguardend_b_load,
        object_anim_defs_deskslider_load, object_anim_defs_magnum_load, object_anim_defs_rifle_load,
        object_anim_defs_tippergunaway_load, object_anim_defs_standpose_load, object_anim_defs_gondola2_load,
        object_anim_defs_trainambient_load, object_anim_defs_brick_load, object_anim_defs_look_m0_load,
        object_anim_defs_irisdoors_load, object_anim_defs_luger_load, object_anim_defs_plaspist_load,
        object_anim_defs_sbp500_load, object_anim_defs_dozer_load, object_anim_defs_drunkguard_load,
        object_anim_defs_machinepistol_load, object_anim_defs_zomambleattack_load, object_anim_defs_scientistscared_load,
        object_anim_defs_tippertalk_load, object_anim_defs_shotgunback_load, object_anim_defs_alertguy_load,
        object_anim_defs_strapflaps_load, object_anim_defs_peekboxlft_load, object_anim_defs_peekboxlft2_load,
        object_anim_defs_harpoon_load, object_anim_defs_fsniper_load, object_anim_defs_heatseeker_load,
        object_anim_defs_jbcoweridle_load, object_anim_defs_doublesg_load, object_anim_defs_jobethcaseyhands_load,
        object_anim_defs_jbarmsflapping_load, object_anim_defs_jungleqcellblast_load, object_anim_defs_drowningdeath_load,
        object_anim_defs_kneelreact_load, object_anim_defs_r110drcontwalk_load, object_anim_defs_flightcycle_load,
        object_anim_defs_ashcowerrun_load, object_anim_defs_flightcycle_803da908_load, object_anim_defs_ashcowerrun_803dabd8_load,
        object_anim_defs_mattackheadbutt_load, object_anim_defs_monkeyiceskit_load, object_anim_defs_girlroachattack_load,
        object_anim_defs_magchargeidle_load, object_anim_defs_devgirlheadshake_load, object_anim_defs_jbeverybodyout_load,
        object_anim_defs_uplinkidle_load, object_anim_defs_zomgroundspawn_load, object_anim_defs_startflyby_load,
        object_anim_defs_gren_back_todive_load, object_anim_defs_ashwindowfall_load, object_anim_defs_ashcower_load,
        object_anim_defs_jbattackwalk360_load, object_anim_defs_ashbinocularidle_load, object_anim_defs_zomattack01_load,
        object_anim_defs_croweidle_load, object_anim_defs_jbcampullout_load, object_anim_defs_ghostgun_idle1_load,
        object_anim_defs_periscopetapsoldier_load, object_anim_defs_zomcartattack_load, object_anim_defs_9mmauto_shot_load,
        object_anim_defs_hatchopen_load, object_anim_defs_flamethrow_idle_load, object_anim_defs_nemosmg_32shot_load,
        object_anim_defs_botshock_load, object_anim_defs_dartgun_idle_load, object_anim_defs_bomb_idle_load,
        object_anim_defs_flightcycle_803de328_load, object_anim_defs_flightcycle_803de328_load_b,
        object_anim_defs_cortezpovidle_cam_load, object_anim_defs_r110_run_803ded90_load,
    };
    for (int i = 0; i < (int)(sizeof(kLoaders) / sizeof(kLoaders[0])); ++i) {
        unsigned long long pair = __ow_pack_find_by_name();
        int index = (int)(pair >> 0x20);
        int id = (int)pair;
        if (index != -1) {
            kLoaders[i](param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8,
                        (unsigned int)index, (unsigned int)id, param_11, param_12, param_13, param_14,
                        param_15, param_16);
        }
    }
}
// ===========================================================================
// Address: 0x801bfbe8 | Name: object_anim_defs_reset_state | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Zero-initialize the 0xc3c-byte object-weapon anim-defs state block
// (DAT_805c1a28). Called during subsystem reset to clear the registered
// pack/descriptor bookkeeping before object_anim_defs_register_all_packs runs.
// ===========================================================================
void object_anim_defs_reset_state(void)
{
    memset_wrapper((void *)0x805c1a28, 0, 0xc3c);
}
