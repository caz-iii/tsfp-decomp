// Address: 0x800061bc | Name: get_string_by_id | Date: 2026-07-23
// Address: 0x8000651c | Name: FUN_8000651c (clear_string_table) | Date: 2026-07-23
// Address: 0x80006528 | Name: FUN_80006528 (load_string_table) | Date: 2026-07-23
// Description: String localization system.
// get_string_by_id: Looks up localized string by ID. 1791 cross-references.
//   - IDs > 0xD2B: story strings (loaded per-level from DAT_806115a8 + offset)
//   - IDs 1..0xD2B: common strings (from DAT_806115b0)
//   - ID 0: returns hardcoded fallback address
//   - If table not loaded: returns DAT_8060f998 (empty string)
//
// FUN_8000651c: Clears the common string table pointer (DAT_806115b0 = 0)
// FUN_80006528: Loads string table from disk based on language/game mode.
//   References paths like "data\\str\\ts_<lang>\\<category>.bs"

#include "common.h"

extern s32 DAT_806115a8;  // story string table base
extern s32 DAT_806115b0;  // common string table base
extern s32 DAT_8060f998;  // fallback empty string

// Get localized string pointer by ID
s32 get_string_by_id(s32 id) {
    if (DAT_806115a8 == 0) {
        return DAT_8060f998;
    }
    if (id > 0xD2B) {
        // Story string: offset from story table
        return *(s32*)(DAT_806115a8 + id * 4 - 0x34B0);
    }
    if (id == 0) {
        return 0x8060F984;  // hardcoded fallback
    }
    if (DAT_806115b0 != 0) {
        // Common string: direct lookup
        return *(s32*)(DAT_806115b0 + id * 4);
    }
    return DAT_8060f998;
}

// Clear common string table
void clear_string_table(void) {
    DAT_806115b0 = 0;
}
