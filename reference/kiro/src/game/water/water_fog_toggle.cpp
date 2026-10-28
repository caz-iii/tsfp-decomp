// water_fog_toggle.cpp
// Underwater-fog enable/disable toggles. Set the water-fog flag DAT_80611f10
// and re-apply the water fog state via water_surface_fog_apply.
//
// Batch: batch_495
#include "common.h"

extern "C" {
extern void water_surface_fog_apply(void);
extern int  DAT_80611f10;  // underwater-fog enabled flag
}

// ---------------------------------------------------------------------------
// Address: 0x8018de4c | Name: water_fog_enable | Date: 2026-09-06
// Enable underwater fog and re-apply the fog state. Returns its argument.
// ---------------------------------------------------------------------------
int water_fog_enable(int arg)
{
    DAT_80611f10 = 1;
    water_surface_fog_apply();
    return arg;
}

// ---------------------------------------------------------------------------
// Address: 0x8018de84 | Name: water_fog_disable | Date: 2026-09-06
// Disable underwater fog and re-apply the fog state.
// ---------------------------------------------------------------------------
void water_fog_disable(void)
{
    DAT_80611f10 = 0;
    water_surface_fog_apply();
}
