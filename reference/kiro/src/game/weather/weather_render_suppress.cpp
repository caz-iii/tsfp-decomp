// Address: 0x80357874 | Name: weather_set_enable_flag       | Date: 2026-09-12
// Address: 0x8035787c | Name: weather_clear_enable_flag     | Date: 2026-09-12
// Address: 0x80357888 | Name: weather_suppress_reset        | Date: 2026-09-12
// Address: 0x80357898 | Name: weather_suppress_add_forced   | Date: 2026-09-12
// Address: 0x803578bc | Name: weather_suppress_apply        | Date: 2026-09-12
// Address: 0x80357980 | Name: weather_suppress_restore      | Date: 2026-09-12
// Address: 0x80357a48 | Name: weather_is_enabled            | Date: 2026-09-12
//
// Weather render-suppress helpers used during the lightning/flare render pass.
// The system temporarily hides a small set of tracked entities (so the flare
// glow can be composited without them) and force-shows up to two "always
// visible" entities. Entity render flags live at entity +0x28 (bit 0x800000 =
// hidden/suppressed, bit 1 = force-visible). Entity handles are validated via
// the entity table at DAT_80611c24 (0x2e8-stride, generation id at +0x2c).
//
//   DAT_80612de4  : weather-enabled flag (set by the mode)
//   DAT_80612df8  : count of tracked (to-hide) entity handles
//   DAT_80612df0[]: tracked entity handle array
//   DAT_80612de8[]: saved prior hidden-bit per tracked entity
//   DAT_80612de0  : count of forced-visible entity pointers (max 2)
//   DAT_80612dd8[]: forced-visible entity pointer array
//   DAT_80612dfc  : suppress-active flag

#include "common.h"

extern int DAT_80612de4;        // weather-enabled flag
extern int DAT_80612df8;        // tracked-entity count
extern int DAT_80612df0;        // tracked entity handle array base
extern int DAT_80612de8;        // saved hidden-bit array base
extern int DAT_80612de0;        // forced-visible count
extern int DAT_80612dd8;        // forced-visible pointer array base
extern int DAT_80612dfc;        // suppress-active flag
extern int DAT_80611c24;        // entity table base (0x2e8-stride)

// ============================================================================
// weather_set_enable_flag  (0x80357874)
// ============================================================================
void weather_set_enable_flag(int value)
{
    DAT_80612de4 = value;
}

// ============================================================================
// weather_clear_enable_flag  (0x8035787c)
// ============================================================================
void weather_clear_enable_flag(void)
{
    DAT_80612de4 = 0;
}

// ============================================================================
// weather_suppress_reset  (0x80357888)
// Clear both the tracked and forced-visible lists.
// ============================================================================
void weather_suppress_reset(void)
{
    DAT_80612df8 = 0;
    DAT_80612de0 = 0;
}

// ============================================================================
// weather_suppress_add_forced  (0x80357898)
// Add an entity pointer to the forced-visible list (capacity 2).
// ============================================================================
void weather_suppress_add_forced(int entity)
{
    if (1 < DAT_80612de0) {
        return;
    }
    int idx = DAT_80612de0 * 4;
    DAT_80612de0 = DAT_80612de0 + 1;
    *(int*)(&DAT_80612dd8 + idx) = entity;
}

// ============================================================================
// weather_suppress_apply  (0x803578bc)
// When suppression is active, save each tracked entity's current hidden bit and
// set it (0x800000), then set the force-visible bit (1) on every forced entity.
// Stale handles (generation mismatch at +0x2c) are dropped.
// ============================================================================
void weather_suppress_apply(void)
{
    if (DAT_80612dfc != 1) {
        return;
    }
    int off = 0;
    for (int i = 0; i < DAT_80612df8; i++) {
        unsigned int handle = *(unsigned int*)(&DAT_80612df0 + off);
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) {
                ent = 0;
                *(int*)(&DAT_80612df0 + off) = 0;
            }
        }
        if (ent != 0) {
            *(unsigned int*)(&DAT_80612de8 + off) = *(unsigned int*)(ent + 0x28) & 0x800000;
            *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) | 0x800000;
        }
        off += 4;
    }
    off = 0;
    for (int i = 0; i < DAT_80612de0; i++) {
        int* pEnt = (int*)(&DAT_80612dd8 + off);
        off += 4;
        *(unsigned int*)(*pEnt + 0x28) = *(unsigned int*)(*pEnt + 0x28) | 1;
    }
}

// ============================================================================
// weather_suppress_restore  (0x80357980)
// Undo weather_suppress_apply: clear the hidden bit then OR back the saved
// prior value for each tracked entity, and clear the force-visible bit on each
// forced entity.
// ============================================================================
void weather_suppress_restore(void)
{
    if (DAT_80612dfc != 1) {
        return;
    }
    int off = 0;
    for (int i = 0; i < DAT_80612df8; i++) {
        unsigned int handle = *(unsigned int*)(&DAT_80612df0 + off);
        int ent;
        if (handle == 0) {
            ent = 0;
        } else {
            ent = DAT_80611c24 + (handle >> 0x16) * 0x2e8;
            if (*(unsigned int*)(ent + 0x2c) != handle) {
                ent = 0;
                *(int*)(&DAT_80612df0 + off) = 0;
            }
        }
        if (ent != 0) {
            *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) & 0xff7fffff;
            *(unsigned int*)(ent + 0x28) = *(unsigned int*)(ent + 0x28) | *(unsigned int*)(&DAT_80612de8 + off);
        }
        off += 4;
    }
    off = 0;
    for (int i = 0; i < DAT_80612de0; i++) {
        int* pEnt = (int*)(&DAT_80612dd8 + off);
        off += 4;
        *(unsigned int*)(*pEnt + 0x28) = *(unsigned int*)(*pEnt + 0x28) & 0xfffffffe;
    }
}

// ============================================================================
// weather_is_enabled  (0x80357a48)
// Return true iff the weather-enabled flag is set.
// ============================================================================
bool weather_is_enabled(void)
{
    return DAT_80612de4 != 0;
}
