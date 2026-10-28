// Batch 343 | Date: 2026-09-04
// Spawn helper for the HUD/objective marker entity (type 0xe5). Called from the
// objective/marker setup path (FUN_80127180 group). Spawns the marker entity at
// the caller-supplied transform with a zeroed local scale/offset vector and
// stashes the resulting entity pointer in the global DAT_80611f78.
// Subsystem: game (spawn)
//
// Compile-Clean Standard: valid signatures + faithful body; raw symbols declared
// per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float FLOAT_80615730;   // 0.0
extern int   DAT_80611f78;     // last-spawned HUD marker entity ptr

extern int   entity_spawn_by_type(unsigned long long p1, double x, double y,
                                   unsigned long long p4, unsigned long long p5,
                                   unsigned long long p6, unsigned long long p7,
                                   unsigned long long p8, int type,
                                   float* scaleVec, float* offsetVec, int a, int b);

// forward decl (this file)
extern "C" void spawn_hud_marker(unsigned long long p1, double p2, double p3,
                                 unsigned long long p4, unsigned long long p5,
                                 unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8);

// ============================================================================
// 0x8012560c | spawn_hud_marker
// Spawns the type-0xe5 HUD marker entity at the given transform, passing a
// zeroed 3-float scale/offset vector, and records the new entity in DAT_80611f78.
// ============================================================================
extern "C" void spawn_hud_marker(unsigned long long p1, double p2, double p3,
                                 unsigned long long p4, unsigned long long p5,
                                 unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8) {
    float vec[3];
    vec[0] = FLOAT_80615730;
    vec[1] = FLOAT_80615730;
    vec[2] = FLOAT_80615730;
    DAT_80611f78 = entity_spawn_by_type(p1, p2, p3, p4, p5, p6, p7, p8, 0xe5,
                                        vec, vec, 0, 0);
    return;
}
