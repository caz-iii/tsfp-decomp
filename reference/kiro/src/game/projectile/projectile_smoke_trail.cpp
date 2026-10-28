// Address: 0x803591a8 | Name: projectile_spawn_smoke_puff     | Date: 2026-09-12
//
// Projectile smoke-puff trail spawn. Called from projectile_spawn and
// projectile_tick_update to emit a single smoke impact sprite behind a moving
// projectile: the spawn point is the projectile origin (proj+0x64/0x68/0x6c)
// offset by its trail-offset vector (proj+0x1a8/0x1ac/0x1b0), with the two
// horizontal components randomly jittered (scaled by proj+0x1c0 / +0x1c8), and
// a fixed upward bias (FLOAT_8061aec4). Uses smoke sprite 0x3bb and the smoky
// grey colour 0x7f1e1e32.

#include "common.h"

// ---- constants -------------------------------------------------------------
extern float  DAT_803e0c18, DAT_803e0c1c, DAT_803e0c20;  // ray direction template
extern float  FLOAT_8061aeb8;   // jitter amplitude
extern float  FLOAT_8061aec4;   // vertical bias
extern float  FLOAT_8061aef0;   // jitter bias
extern float  FLOAT_8061af04;   // jitter denom
extern double DOUBLE_8061af08;  // int->double bias

// ---- helpers ---------------------------------------------------------------
extern int  rng_counter_tick(void);
extern int  sprite_get_texture_id(int ref);
extern void particle_spawn_impact_from_ray(int texId, int a, int color, int flags, int b,
                                           unsigned int surfaceId, float* pos, float* ray);

// ============================================================================
// projectile_spawn_smoke_puff  (0x803591a8)
// ============================================================================
void projectile_spawn_smoke_puff(int proj)
{
    float ray[3];
    ray[0] = DAT_803e0c18;
    *(int*)&ray[1] = *(int*)&DAT_803e0c1c;
    *(int*)&ray[2] = *(int*)&DAT_803e0c20;

    float pos[3];
    pos[0] = *(float*)(proj + 100)   + *(float*)(proj + 0x1a8);
    pos[1] = *(float*)(proj + 0x68)  + *(float*)(proj + 0x1ac);
    pos[2] = *(float*)(proj + 0x6c)  + *(float*)(proj + 0x1b0);

    pos[1] = pos[1] + FLOAT_8061aec4;

    int r = rng_counter_tick();
    pos[0] = *(float*)(proj + 0x1c0) * FLOAT_8061aeb8 *
             ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061af08) / FLOAT_8061af04 - FLOAT_8061aef0) +
             pos[0];
    r = rng_counter_tick();
    pos[2] = *(float*)(proj + 0x1c8) * FLOAT_8061aeb8 *
             ((float)((double)CONCAT44(0x43300000, (unsigned int)r) - DOUBLE_8061af08) / FLOAT_8061af04 - FLOAT_8061aef0) +
             pos[2];

    int tex = sprite_get_texture_id(0x3bb);
    particle_spawn_impact_from_ray(tex, 1, 0x7f1e1e32, 4, 1, *(unsigned int*)(proj + 0x34), pos, ray);
}
