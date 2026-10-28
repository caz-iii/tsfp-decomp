// worm_batch781.cpp
// Two small state-setter callbacks recovered in batch_781. Kept in their own
// translation unit (the sibling legacy files worm_render.cpp / worm_ghz_state.cpp
// predate the compile-clean standard and do not yet compile); this file meets the
// Compile-Clean Standard on its own and will fold back into those modules when
// they are hardened.
//
// Functions:
//   worm_spawn254_set_door_state (0x801dc7d8) src/game/core/worm_render.cpp module
//   ghz_reactor_spark_jet_enable (0x802093ec) src/game/core/worm_ghz_state.cpp module

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw symbols (types match the call sites in these fns).
// ---------------------------------------------------------------------------
extern int   DAT_80612688;    // root game-state block; +0x658 -> object list root
extern float FLOAT_806171ac;  // "open/active" door pose value (1.0)
extern float FLOAT_80617194;  // "closed/default" door pose value (0.0)
extern int   DAT_806127d8;    // GHZ reactor active flag (gates spark-jet tick)

// ---------------------------------------------------------------------------
// 0x801dc7d8  worm_spawn254_set_door_state
// Spawn-0x254 door-pair visual state setter. Reaches the door object via
// DAT_80612688+0x658 -> +0x7c; when bActive != 0 sets the open pose
// (obj+0x10 = 1.0, obj+0xa0 = 1), otherwise the closed/default pose
// (obj+0x10 = 0.0, obj+0xa0 = 0). Called from the apply path in
// worm_cdh1_spawn254_net_cb (0x801dc814).
// ---------------------------------------------------------------------------
void worm_spawn254_set_door_state(int bActive)
{
    int door = *(int *)(*(int *)(DAT_80612688 + 0x658) + 0x7c);

    if (bActive != 0) {
        *(float *)(door + 0x10) = FLOAT_806171ac;
        *(int   *)(door + 0xa0) = 1;
        return;
    }

    *(float *)(door + 0x10) = FLOAT_80617194;
    *(int   *)(door + 0xa0) = 0;
}

// ---------------------------------------------------------------------------
// 0x802093ec  ghz_reactor_spark_jet_enable
// Sets the GHZ reactor active flag DAT_806127d8 = 1, which gates
// ghz_reactor_spark_jet_tick (0x80209424). The flag is initialised/cleared in
// ghz_level_init.
// ---------------------------------------------------------------------------
void ghz_reactor_spark_jet_enable(void)
{
    DAT_806127d8 = 1;
}
