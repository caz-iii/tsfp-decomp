// Address: 0x803529ec | Name: objective_weapon_extra_zone_mask | Date: 2026-09-12
// Batch: batch_1006
//
// Objective prop/weapon visibility-zone resolution. Continues the objective
// marker / wave / trigger / tick / visibility / entity-setup subsystem. These
// functions compute which render "zones" an objective weapon or prop lights up,
// from its weapon/prop definition, and (for props) drop the prop to the ground.
//
// The render-node zone table is reached via entity +0x04 -> +0x04 (a int* whose
// [0] is the entry count and whose entries are 0x30 dwords each; the visibility
// bitfield is at +0x14 of entry index i, i.e. base[-count*0x30 + i*0x30 + 0x14]).
// The render-node visibility mask is at entity +0x04 +0x30.

#include "common.h"

// ---- raw symbols ----
extern int   DAT_80612614;      // current level id
extern int   DAT_80612d68;      // active special-objective record
extern int   DAT_805d59a0;      // objective->weapon/prop mapping table
extern int   DAT_80611aa8;      // zone descriptor table base (0xb4 stride)
extern int   DAT_80611b04;      // zone entity table base (4-byte ptr entries)
extern float FLOAT_8061ad00;    // 0.0f
extern float FLOAT_8061ad24;    // vertical probe step
extern float FLOAT_8061ad50;    // ground clearance
extern float DAT_803e0bcc;      // ray direction x
extern float DAT_803e0bd0;      // ray direction y
extern float DAT_803e0bd4;      // ray direction z
extern int   DAT_80482690;      // prop zone-group descriptor table (0x68 stride, 4 entries)

// ---- named cross-module helpers ----
extern unsigned char * weapon_get_def_by_index(int index);
extern int  prop_def_by_index(int index);
extern int  scene_raycast(int space, int origin, float *dir, int flags, float *outT,
                          float *outPos, int *outZone, int a8, int a9, int *a10);
extern void prop_spawn_place(void);
extern int  mtx4_set_identity(int pMtx);

// Small 2-arg lookup helpers defined in the following batch (raw externs).
extern "C" int FUN_8035330c(int table, int key);   // objective -> weapon index
extern "C" int FUN_8035332c(int table, int key);   // objective -> prop index

// Register-pair context stubs.
extern "C" int  FUN_803826f0(void);
extern "C" void FUN_8038273c(void);
extern "C" int  __save_gpr_28(void);
extern "C" void __restore_gpr_28(void);

// Helper: zone bitfield for entry `idx` in the render-node zone table `zones`.
static inline u32 zone_bits(int *zones, int idx)
{
    return (u32)zones[*zones * -0x30 + idx * 0x30 + 0x14];
}

// ---------------------------------------------------------------------------
// Extra visibility-zone mask contributed by a weapon/prop class code, resolved
// against the entity's render-node zone table. Handles the special objective
// modes (level 0x66) and the sub-state selector switch.
uint objective_weapon_extra_zone_mask(int classCode, int pEntity)
{
    uint mask = 0;
    int *zones = *(int **)(*(int *)(pEntity + 4) + 4);
    if (classCode == 10) {
        mask = (u32)zones[*zones * -0x30 + 0x284] | (u32)zones[*zones * -0x30 + 0x2b4];
    } else if (classCode < 10) {
        if (classCode == 8) {
            mask = (u32)zones[*zones * -0x30 + 0x1c4] | (u32)zones[*zones * -0x30 + 500];
        } else if (7 < classCode) {
            mask = (u32)zones[*zones * -0x30 + 0x224] | (u32)zones[*zones * -0x30 + 0x254];
        }
    } else if (classCode < 0x14) {
        if (classCode < 0xe) {
            mask = (u32)zones[*zones * -0x30 + 0x44];
        }
    } else if (classCode < 0x17) {
        if (*(int *)(pEntity + 0x7c) == 0) {
            if ((DAT_80612d68 != 0) && (DAT_80612614 == 0x66)) {
                int sub = *(int *)(DAT_80612d68 + 0x88);
                if (sub == 0x33) {
                    mask = (u32)zones[*zones * -0x30 + 0x374];
                } else if (sub < 0x33) {
                    if (0x31 < sub) mask = (u32)zones[*zones * -0x30 + 0xd4];
                } else if (sub < 0x35) {
                    mask = (u32)zones[*zones * -0x30 + 0x344];
                }
            }
        } else {
            switch (*(u32 *)(*(int *)(*(int *)(pEntity + 0x7c) + 0x6c) + 0x30)) {
            case 4: case 0x10: case 0x3a:
                mask = (u32)zones[*zones * -0x30 + 0x374]; break;
            case 10:
                mask = (u32)zones[*zones * -0x30 + 0x314]; break;
            case 0xd:
                mask = (u32)zones[*zones * -0x30 + 0x344]; break;
            case 0x16: case 0x18:
                mask = (u32)zones[*zones * -0x30 + 0x3a4]; break;
            case 0x2a:
                mask = (u32)zones[*zones * -0x30 + 0xd4]; break;
            case 0x34: case 0x40:
                mask = (u32)zones[*zones * -0x30 + 500]; break;
            }
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Compute and OR into the render-node mask the set of zones lit by an objective
// weapon: the weapon def's zone list (weaponDef +0x50, 0xc stride, count +0x4c)
// plus the class extra-zone mask.
void objective_setup_weapon_visibility(void)
{
    int ctx = __save_gpr_28();
    u32 mask = 0;
    int index = -1;
    int *zones = *(int **)(*(int *)(ctx + 4) + 4);

    if (*(int *)(ctx + 0x7c) == 0) {
        if ((DAT_80612d68 != 0) && (DAT_80612614 == 0x66)) {
            index = FUN_8035330c(DAT_805d59a0, *(int *)(DAT_80612d68 + 0x88));
        }
    } else {
        index = *(int *)(*(int *)(ctx + 0x7c) + 0xc);
    }
    unsigned char *def = weapon_get_def_by_index(index);
    int count = *(int *)(def + 0x4c);
    int off = 0;
    if (0 < count) {
        do {
            int fieldOff = off + 0x50;
            off = off + 0xc;
            mask = mask | zone_bits(zones, *(int *)(def + fieldOff));
            count = count + -1;
        } while (count != 0);
    }
    u32 extra = objective_weapon_extra_zone_mask(index, ctx);
    *(u32 *)(*(int *)(ctx + 4) + 0x30) = *(u32 *)(*(int *)(ctx + 4) + 0x30) | mask | extra;
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// True if the entity class id (+0x94) is in the prop range 0x89c..0x8a7.
u32 objective_is_prop_class(int pEntity)
{
    if (0x8a7 < *(unsigned short *)(pEntity + 0x94)) return 0;
    if (*(unsigned short *)(pEntity + 0x94) < 0x89c) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Drop a prop to the ground: nudge it up, raycast down, and either snap to the
// hit point (updating its zone) or rest on the zone's floor height; then place.
void objective_prop_drop_to_ground(int pEntity)
{
    int outZone;
    float outPos[3];
    float outT;
    float dirX, dirY, dirZ;
    float hitY;
    float unusedZ;

    dirX = DAT_803e0bcc;
    dirY = DAT_803e0bd0;
    dirZ = DAT_803e0bd4;
    (void)dirY; (void)dirZ; (void)unusedZ;
    *(float *)(pEntity + 0x68) = *(float *)(pEntity + 0x68) + FLOAT_8061ad24;
    int hit = scene_raycast(*(int *)(pEntity + 0x34), pEntity + 100, &dirX, 1, &outT,
                            outPos, &outZone, 0, 0, (int *)0x0);
    if (hit == 0) {
        *(float *)(pEntity + 0x68) =
            (*(float *)(*(int *)(DAT_80611b04 + *(int *)(pEntity + 0x34) * 4) + 0x68) +
             *(float *)(DAT_80611aa8 + *(int *)(pEntity + 0x34) * 0xb4 + 0x8c)) - FLOAT_8061ad50;
    } else {
        *(int *)(pEntity + 0x34) = outZone;
        *(float *)(pEntity + 100) = outT;
        hitY = outPos[0];
        *(float *)(pEntity + 0x68) = hitY;
        *(float *)(pEntity + 0x6c) = outPos[1];
    }
    prop_spawn_place();
}

// ---------------------------------------------------------------------------
// Post-setup for a ground prop: on level 100, if it is a prop-class entity drop
// it to the ground; classes 0x8a2..0x8a7 also force their render mask visible.
void objective_setup_prop_ground(int pEntity)
{
    if ((DAT_80612614 == 100) && (objective_is_prop_class(pEntity) != 0)) {
        objective_prop_drop_to_ground(pEntity);
    }
    if ((*(unsigned short *)(pEntity + 0x94) < 0x8a8) && (0x8a1 < *(unsigned short *)(pEntity + 0x94))) {
        *(u32 *)(*(int *)(pEntity + 4) + 0x30) = *(u32 *)(*(int *)(pEntity + 4) + 0x30) | 1;
    }
}

// ---------------------------------------------------------------------------
// Compute an objective prop's lit-zone mask (special mode 0x66) from its prop
// definition: primary/secondary zone fields (+0x10/+0x20/+0x24), extra masks
// (+0x7c/+0x84/+0x8c) and a per-def zone list (+0x34, 0xc stride, count +0x30);
// then run the def's optional setup callback (+0xb0).
void objective_setup_prop_visibility(void)
{
    unsigned char scratch[88];
    int ctx = __save_gpr_28();
    int index = -1;
    u32 mask = 0;
    int *zones = *(int **)(*(int *)(ctx + 4) + 4);

    if ((DAT_80612614 == 100) && (objective_is_prop_class(ctx) != 0)) {
        objective_prop_drop_to_ground(ctx);
    }
    mtx4_set_identity((int)scratch);
    if (DAT_80612614 == 0x66) {
        if (DAT_80612d68 != 0) {
            index = FUN_8035332c(DAT_805d59a0, *(int *)(DAT_80612d68 + 0x88));
        }
        int def = prop_def_by_index(index);
        if (-1 < *(int *)(def + 0x10)) {
            mask = zone_bits(zones, *(int *)(def + 0x10));
        }
        if (-1 < *(int *)(def + 0x20)) {
            mask = mask | zone_bits(zones, *(int *)(def + 0x20));
        }
        if (-1 < *(int *)(def + 0x24)) {
            mask = mask | zone_bits(zones, *(int *)(def + 0x24)) |
                   *(u32 *)(def + 0x8c) | *(u32 *)(def + 0x7c);
        }
        int off = 0;
        int count = *(int *)(def + 0x30);
        mask = mask | *(u32 *)(def + 0x84);
        if (0 < count) {
            do {
                int fieldOff = off + 0x34;
                off = off + 0xc;
                mask = mask | zone_bits(zones, *(int *)(def + fieldOff));
                count = count + -1;
            } while (count != 0);
        }
        *(u32 *)(*(int *)(ctx + 4) + 0x30) = *(u32 *)(*(int *)(ctx + 4) + 0x30) | mask;
        int (*cb)(int) = *(int (**)(int))(def + 0xb0);
        if (cb != nullptr) {
            cb(ctx);
        }
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// Find the prop zone-group descriptor whose first word matches key in the
// 4-entry table at DAT_80482690 (0x68 stride). Returns null if none.
int * objective_find_prop_group(int key)
{
    int off = 0;
    int i = 4;
    do {
        if (key == *(int *)((int)&DAT_80482690 + off)) {
            return (int *)((int)&DAT_80482690 + off);
        }
        off = off + 0x68;
        i = i + -1;
    } while (i != 0);
    return (int *)0x0;
}

// Find, within a prop group, the 0xc-byte entry (8 max) whose id (+0x04) matches.
int * objective_find_prop_zone_entry(int pGroup, int key)
{
    int off = 0;
    int i = 8;
    int *entry;
    do {
        entry = (int *)(pGroup + off + 4);
        if (key == *entry) {
            return entry;
        }
        off = off + 0xc;
        i = i + -1;
    } while (i != 0);
    return (int *)0x0;
}

// ---------------------------------------------------------------------------
// Update a prop's render zone mask over its lifetime: while the fade timer
// (+0x1cc) has expired, clear the group's "on" zone (group[0x19]); otherwise set
// it. When both alt zones are valid, add one and remove the other.
void objective_update_prop_zone_mask(void)
{
    int ctx = FUN_803826f0();
    int state = *(int *)(ctx + 0x7c);
    int *zones = *(int **)(*(int *)(ctx + 4) + 4);
    int *group = objective_find_prop_group(*(int *)(state + 0xc));
    int *entry = objective_find_prop_zone_entry((int)group, *(int *)(*(int *)(state + 0x6c) + 0x30));

    int onZone, offZone;
    u32 *pMask = (u32 *)(*(int *)(ctx + 4) + 0x30);
    if (*(float *)(ctx + 0x1cc) <= FLOAT_8061ad00) {
        offZone = entry[2];
        onZone = entry[1];
        *pMask = *pMask & ~zone_bits(zones, group[0x19]);
    } else {
        offZone = entry[1];
        onZone = entry[2];
        *pMask = *pMask | zone_bits(zones, group[0x19]);
    }
    if ((-1 < offZone) && (-1 < onZone)) {
        *pMask = *pMask | zone_bits(zones, offZone);
        *pMask = *pMask & ~zone_bits(zones, onZone);
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// Apply the prop zone mask: in special mode 0x66 select the zone index by the
// objective->prop lookup (0x18 -> 0xb, 0x19 -> 10) and OR it into the mask;
// otherwise defer to objective_update_prop_zone_mask.
void objective_apply_prop_zone_mask(int pEntity)
{
    if ((DAT_80612d68 == 0) || (DAT_80612614 != 0x66)) {
        objective_update_prop_zone_mask();
    } else {
        int zoneIdx = -1;
        int sel = FUN_8035332c(DAT_805d59a0, *(int *)(DAT_80612d68 + 0x88));
        int node = *(int *)(pEntity + 4);
        if (sel == 0x19) {
            zoneIdx = 10;
        } else if ((sel < 0x19) && (0x17 < sel)) {
            zoneIdx = 0xb;
        }
        int *zones = *(int **)(node + 4);
        *(u32 *)(node + 0x30) = *(u32 *)(node + 0x30) | zone_bits(zones, zoneIdx);
    }
}
