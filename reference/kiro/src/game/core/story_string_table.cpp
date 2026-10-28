// Address: 0x800067d0 | Name: FUN_800067d0 (load_story_string_table) | Date: 2026-07-23
// Address: 0x80006988 | Name: FUN_80006988 (preload_string_tables) | Date: 2026-07-23
// Description: Loads story-specific localized string table from disk.
// Path format: "data\\str\\ts_<lang>\\<platform>\\story.bs"
// References DAT_8060f978 (platform string), DAT_8060f99c/DAT_8060f9a0 (language strings).
// PTR_DAT_803e1f68 = array of 5 platform name pointers.
// DAT_806115a8 = story string table pointer
// DAT_806115ac = story string table size
// Table has 0x102C entries (4140 strings) with relative offsets fixed up after load.
//
// FUN_80006988: Preloads/caches both story and common string tables
// for all language/platform combinations.

#include "common.h"

// See raw decompilation for full implementation.
// Key operations:
//   1. Build path string using sprintf-like FUN_80387fb8
//   2. Try loading from DVD cache first (FUN_8002d410)
//   3. If not cached: allocate temp buffer, load from disk, copy to permanent allocation
//   4. Fix up relative string offsets to absolute pointers (0x102C entries for story)
