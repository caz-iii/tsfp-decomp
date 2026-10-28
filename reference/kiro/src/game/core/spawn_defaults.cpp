// spawn_defaults.cpp
// Default entity-class query stubs for the spawn subsystem, recovered in
// batch_809. Both are single-instruction accessors used as default virtual /
// callback slot implementations, called directly from the spawn lifecycle
// paths (spawn_respawn_anim_tick 0x80096cd4, spawn_on_destroy 0x80093eac).
//
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// 0x802bc194  spawn_default_query_invalid
// Default "no value" query: returns 0xffffffff (-1), the sentinel used across
// the spawn code for "no handle / invalid id".
// ---------------------------------------------------------------------------
unsigned int spawn_default_query_invalid(void)
{
    return 0xffffffff;
}

// ---------------------------------------------------------------------------
// 0x802bc19c  spawn_default_query_zero
// Default "false / zero" query: returns 0. Used as the do-nothing default for
// spawn callback slots that report a count or boolean.
// ---------------------------------------------------------------------------
unsigned int spawn_default_query_zero(void)
{
    return 0;
}
