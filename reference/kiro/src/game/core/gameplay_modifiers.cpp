// gameplay_modifiers.cpp
// Gameplay-modifier / feature-set flag system. Address range 0x801c0818 -
// 0x801c09a8 (batch_585). Tracks a 16-bit active-modifier mask (DAT_80612600)
// and a resolved "available features" mask (DAT_806125fc) derived from a
// 16-entry descriptor table (DAT_8044ee18, 0x1c stride). The active mask gates
// per-bone model scaling (model_apply_part_scales / batch_387) and other
// gameplay tweaks (big-head, etc.); model_apply_part_scales reads the mask via
// gameplay_modifier_get_mask.
//
// The descriptor table holds, at record[0] (word), the game-feature flag set
// that each of the 16 modifier slots requires. gameplay_modifier_resolve_mask
// builds a slot-bitmask of every table entry whose required flags intersect a
// query flag word.
//
// Non-matching: the wide FP-context spill frame (FUN_803826f0 / FUN_8038273c)
// is preserved as calls so the ROM stack shape is retained; the two feature-
// slot apply helpers (FUN_801c1314 / FUN_801c12e4) are frontier functions and
// are referenced via file-local externs until decompiled.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {
extern int  DAT_80612600;      // active gameplay-modifier bitmask (16 slots)
extern int  DAT_806125fc;      // resolved "available modifiers" bitmask
extern int  DAT_80612614;      // game-mode id (0x65/0x67 == special modes)
extern int  DAT_805015c0;      // global feature flags
extern char DAT_805015bd;      // region/mode selector ('\n' special)
// 16-entry modifier descriptor table (0x1c stride); record[0] = required
// game-feature flag set for that modifier slot.
extern unsigned int DAT_8044ee18[];

// Frontier feature-slot apply helpers (decompiled by a later batch).
int  FUN_801c1314(int slot);   // apply/query one modifier slot; returns active?
void FUN_801c12e4(void);       // per-active-slot side effect (called x5 for slot 0xc)
} // extern "C"

// Wide FP-context PPC ABI spill frame used by the slot-init sweep.
static inline void __gm_frame_open(void)  { ((void (*)(void))0x803826f0)(); }
static inline void __gm_frame_close(void) { ((void (*)(void))0x8038273c)(); }

// Forward declaration (defined below; used by gameplay_modifier_resolve_available).
unsigned int gameplay_modifier_resolve_mask(unsigned int queryFlags);

// ===========================================================================
// Address: 0x801c0818 | Name: gameplay_modifier_resolve_available | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Resolve the set of available gameplay modifiers for the current game mode
// into DAT_806125fc. Disabled entirely in modes 0x67 and 0x65, and when the
// global feature flags carry the 0x8800040 suppress bits. Otherwise resolves
// against feature-flag 4 in the "special region" (DAT_805015bd == '\n') or
// feature-flag 2 normally.
// ===========================================================================
void gameplay_modifier_resolve_available(void)
{
    DAT_806125fc = 0;
    if (DAT_80612614 == 0x67) return;
    if (DAT_80612614 == 0x65) return;
    if ((DAT_805015c0 & 0x8800040) != 0) return;

    if (DAT_805015bd == '\n') {
        DAT_806125fc = (int)gameplay_modifier_resolve_mask(4);
    } else {
        DAT_806125fc = (int)gameplay_modifier_resolve_mask(2);
    }
}

// ===========================================================================
// Address: 0x801c0894 | Name: gameplay_modifier_init_all | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Initialize all 16 modifier slots via the per-slot apply helper.
// ===========================================================================
void gameplay_modifier_init_all(void)
{
    __gm_frame_open();
    for (int slot = 0; slot < 0x10; ++slot) {
        FUN_801c1314(slot);
    }
    __gm_frame_close();
}

// ===========================================================================
// Address: 0x801c08f4 | Name: gameplay_modifier_get_string_id | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Return the localized string id (0xc4b) for the modifier menu heading.
// ===========================================================================
unsigned int gameplay_modifier_get_string_id(void)
{
    return 0xc4b;
}

// ===========================================================================
// Address: 0x801c08fc | Name: gameplay_modifier_get_mask | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Return the active gameplay-modifier bitmask (read by model_apply_part_scales
// and others).
// ===========================================================================
unsigned int gameplay_modifier_get_mask(void)
{
    return (unsigned int)DAT_80612600;
}

// ===========================================================================
// Address: 0x801c0904 | Name: gameplay_modifier_resolve_mask | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Build a 16-bit slot mask of every descriptor-table entry (DAT_8044ee18,
// 0x1c stride) whose required game-feature flags (record[0]) intersect the
// query flag word.
// ===========================================================================
unsigned int gameplay_modifier_resolve_mask(unsigned int queryFlags)
{
    unsigned int mask = 0;
    int recOff = 0;
    for (int i = 0; i < 0x10; ++i) {
        if ((queryFlags & *(unsigned int *)((int)DAT_8044ee18 + recOff)) != 0) {
            mask |= 1u << i;
        }
        recOff += 0x1c;
    }
    return mask;
}

// ===========================================================================
// Address: 0x801c0950 | Name: gameplay_modifier_get_flags | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Return the required game-feature flag set (record[0]) for modifier slot.
// ===========================================================================
unsigned int gameplay_modifier_get_flags(int slot)
{
    return DAT_8044ee18[slot * 7];   // 0x1c / 4 == 7 words per record
}

// ===========================================================================
// Address: 0x801c0968 | Name: gameplay_modifier_set | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Enable modifier slot in the active mask.
// ===========================================================================
void gameplay_modifier_set(int slot)
{
    DAT_80612600 = DAT_80612600 | (1 << slot);
}

// ===========================================================================
// Address: 0x801c0980 | Name: gameplay_modifier_clear | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Disable modifier slot in the active mask.
// ===========================================================================
void gameplay_modifier_clear(int slot)
{
    DAT_80612600 = DAT_80612600 & ~(1 << slot);
}

// ===========================================================================
// Address: 0x801c0998 | Name: gameplay_modifier_reset | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Clear both the active and available modifier masks.
// ===========================================================================
void gameplay_modifier_reset(void)
{
    DAT_80612600 = 0;
    DAT_806125fc = 0;
}

// ===========================================================================
// Address: 0x801c09a8 | Name: gameplay_modifier_apply_all | Date: 2026-09-07
// ---------------------------------------------------------------------------
// Apply all 16 modifier slots; when slot 0xc is active, run its side effect
// five times.
// ===========================================================================
void gameplay_modifier_apply_all(void)
{
    for (int slot = 0; slot < 0x10; ++slot) {
        int active = FUN_801c1314(slot);
        if (active != 0 && slot == 0xc) {
            for (int i = 0; i < 5; ++i) {
                FUN_801c12e4();
            }
        }
    }
}
