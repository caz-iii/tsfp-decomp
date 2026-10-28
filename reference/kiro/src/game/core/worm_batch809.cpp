// worm_batch809.cpp
// Standalone game-logic helpers recovered in batch_809. Kept in their own
// translation unit so they meet the Compile-Clean Standard independently of the
// sibling legacy worm_* files that predate the standard.
//
// Functions:
//   worm_czs_dist_to_lod_bucket (0x80223658) - CZS collectible render LOD select
//
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw ROM symbols (types match the call site).
// ---------------------------------------------------------------------------
extern double math_sqrt(double x);            // shared math helper (not in decls with this sig)
extern float  FLOAT_80618590;                 // reference/near distance constant
extern float  FLOAT_806185b0;                 // distance -> LOD-index scale factor

// ---------------------------------------------------------------------------
// 0x80223658  worm_czs_dist_to_lod_bucket
// Selects a render LOD "detail count" for a CZS collectible object based on how
// far its owner entity is from a tracked world point. param_1 is the object
// record: [0] -> owner entity block, [10] -> float[3] world point.
//
// The owner entity's aim/muzzle point is (ent+0x1a8/0x1ac/0x1b0) + (ent+0x64/
// 0x68/0x6c) offset; distance to the object point drives an index:
//   idx = (FLOAT_80618590 - dist) * FLOAT_806185b0
// which is then quantised to one of five detail buckets.
//   idx <  0x1a -> 0x19 (25)
//   idx <  0x33 -> 0x32 (50)
//   idx <  0x4c -> 0x4b (75)
//   else        -> 0x64 (100)
// Called from worm_czs_item_collect_render_b (0x8022386c).
// ---------------------------------------------------------------------------
unsigned int worm_czs_dist_to_lod_bucket(int *param_1)
{
    int   ent = param_1[0];
    float *pt = (float *)param_1[10];

    float dy = (*(float *)(ent + 0x1ac) + *(float *)(ent + 0x68)) - pt[1];
    float dx = (*(float *)(ent + 0x1a8) + *(float *)(ent + 0x64)) - pt[0];
    float dz = (*(float *)(ent + 0x1b0) + *(float *)(ent + 0x6c)) - pt[2];

    double dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
    int idx = (int)((float)((double)FLOAT_80618590 - dist) * FLOAT_806185b0);

    unsigned int detail;
    if (idx < 0x4c) {
        if (idx < 0x33) {
            if (idx < 0x1a) {
                detail = 0x19;
            } else {
                detail = 0x32;
            }
        } else {
            detail = 0x4b;
        }
    } else {
        detail = 0x64;
    }
    return detail;
}
