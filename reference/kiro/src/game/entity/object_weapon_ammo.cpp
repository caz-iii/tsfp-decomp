// object_weapon_ammo.cpp
// Weapon-type descriptor queries and ammo/clip accounting for the player object.
// These read the weapon-type descriptor table (DAT_80610ba0, stride 0x29c,
// indexed either directly by type id or through the DAT_8043e23c index table,
// stride 0xf) and the object's per-slot ammo counters, and drive the
// can-fire / need-reload decision used by the weapon FSM. Also here: returning
// a partially-spent clip to the reserve pool, and weapon teardown (fade out
// firing sounds + dispatch the per-hand unequip). Continuation of the object
// weapon module after object_weapon_render.cpp (batch_555).
//
// Address range: 0x801b6544 - 0x801b6e18
// Batch: batch_556
//
// Non-matching translation: the 0x43300000 int->double bias idiom (Ghidra's
// int->double conversion) is reduced to ordinary int/float math, and the
// paired-single f-reg spill temporaries are dropped. Raw field-offset pointer
// arithmetic on the object / weapon-descriptor / slot structures is preserved
// (as elsewhere in the entity module) since those layouts are not yet typed.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h -> match_slot_field_ptr / object_type_dispatch / sound_register_fade_out

// ---- Raw symbols referenced by this translation unit (per-file externs) ----
extern "C" {
// Render/compute spill-frame bracketing (paired push/pop).
extern unsigned long long FUN_803826b4(void);  // spill push: returns (flagbit<<32)|object
extern void               FUN_80382700(void);   // spill pop (matches FUN_803826b4)

// Grenade/timed-fuse helpers used by the ammo-state computation for type 0x23.
extern int FUN_801b7e74(void);            // current fuse/charge frame counter
extern int FUN_801b7e08(int delta, unsigned int budget); // fuse-vs-budget gate -> bool

// ---- Weapon-type descriptor / object globals ----
extern int    DAT_80610ba0;    // weapon-type descriptor table base (stride 0x29c)
extern short  DAT_8043e23c[];  // object-type index table (stride 0xf shorts)
extern char   DAT_805015bd;    // difficulty / rate-table selector byte
extern int    DAT_806109c0;    // frame delta denominator source
extern int    iRam00000390;    // low-mem scratch default (dispatch type default id)

// ---- fp bias / tuning constants ----
extern double DOUBLE_80616ba0; // int->double bias (0x43300000 magic)
extern float  FLOAT_80616ba8;  // 1.0f
extern float  FLOAT_80616bfc;  // charged-shot rate multiplier
extern float  FLOAT_80616c00;  // sound fade-out rate
extern float  FLOAT_80616c0c;  // ramp slope
extern float  FLOAT_80616c74;  // ramp denominator
} // extern "C"

// Fire-rate table (indexed word array reached at DAT_80610ba0-relative negative
// offset in the decomp: base -0x7fbcf73c). Kept as a raw pointer read.
extern "C" float DAT_804308c4[];  // fire-rate LUT (== (int*)-0x7fbcf73c)

// ===========================================================================
// Address: 0x801b6544 | Name: object_weapon_type_slot_flag | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Returns bit0 of the slot field for weapon-type descriptor `type` and slot
// index `slot` (0 if the type id is out of range). Used to test a per-slot
// weapon capability flag.
// ===========================================================================
unsigned int object_weapon_type_slot_flag(int type, int slot)
{
    unsigned int result;

    if ((type < 0) || (0x41 < type)) {
        result = 0;
    } else {
        unsigned int *field = (unsigned int *)match_slot_field_ptr(DAT_80610ba0 + type * 0x29c, slot);
        result = *field & 1;
    }
    return result;
}

// ===========================================================================
// Address: 0x801b6590 | Name: object_weapon_compute_ammo_state | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Computes the dual-hand ammo/clip availability state for the active weapon and
// writes a result block at `resultOut`: for each hand and each of two ammo
// slots it records whether the clip has rounds (near/immediate) and whether the
// reserve can refill, plus a combined "can fire" and "empty" summary. Grenade
// weapons (type 0x23) gate on the fuse counter; charge weapons (type 0x10)
// mirror one slot's state onto the other depending on charge level. The
// per-frame fire budget `budget` is derived from the object's active-channel
// field, adjusted for the alternate weapon when the mask says so.
// ===========================================================================
void object_weapon_compute_ammo_state(unsigned int param_1, unsigned int param_2, int resultOut)
{
    unsigned long long packed;
    int   object;
    unsigned int budget;
    unsigned int useAlt;   // uVar14: 1 unless the alt-weapon compare selects it
    bool  reserveEmpty;    // bVar3
    int   hand;

    (void)param_1;
    (void)param_2;

    packed = FUN_803826b4();
    object = (int)packed;
    budget = *(unsigned int *)(object + 0x2c4);
    useAlt = 1;
    reserveEmpty = false;

    if ((((packed & 0x100000000ULL) == 0) && (*(int *)(object + 0x80) == 0)) &&
        (*(int *)(object + 0xd4) != -1)) {
        unsigned int altBudget = *(unsigned int *)(object + 0x194);
        // signed midpoint select between budget and altBudget
        int diff = ((int)(altBudget ^ budget) >> 1) - (int)((altBudget ^ budget) & altBudget);
        useAlt = (unsigned int)(-1 < diff);
        if (-1 < diff) {
            budget = altBudget;
        }
        budget = budget << 1;
    }

    int local_a8[12];   // per-hand scratch: [hand*2 .. ] slot near/refill flags at +0/+4/+0x10/+0x14/+0x20/+0x24

    int handByteOff = 0;   // iVar20
    for (hand = 0; hand < 2; hand++) {
        int weaponBlock = (hand == 0) ? (object + 0xa4) : (object + 0x1d4);

        int *nearOut   = (int *)((int)local_a8 + handByteOff);
        int *refillOut = (int *)((int)local_a8 + handByteOff + 0x10);
        int *fireOut   = (int *)((int)local_a8 + handByteOff + 0x20);
        fireOut[0]  = 0;
        refillOut[0] = 0;
        nearOut[0]  = 0;
        fireOut[1]  = 0;
        refillOut[1] = 0;
        nearOut[1]  = 0;

        if (((hand != 0) ||
             (((packed & 0x100000000ULL) == 0) && (*(int *)(object + 0x80) == 0))) &&
            (*(int *)(weaponBlock + 0x30) != -1)) {

            int slotByteOff = 0;   // iVar19
            int descBase = DAT_80610ba0 + *(int *)(weaponBlock + 0x30) * 0x29c;

            for (int slot = 0; slot < 2; slot++) {
                int ammoField = *(int *)(descBase + 0x14);
                int ammoMode  = *(int *)(descBase + 0x18);

                if ((slot == 1) &&
                    (*(int *)(descBase + 0x1c) != 0) &&
                    (*(int *)(descBase + 0x1c) != ammoField)) {
                    ammoMode  = *(int *)(descBase + 0x20);
                    ammoField = *(int *)(descBase + 0x1c);
                }

                if (ammoField != 0) {
                    int slotPtr = match_slot_field_ptr(descBase, slot);

                    short count;
                    if ((ammoMode == 0) || (ammoMode == 1)) {
                        count = *(short *)(object + ammoField * 2 + 0x61e);   // reserve pool
                    } else {
                        count = *(short *)(weaponBlock + ammoField * 2 + 0x38); // per-clip
                    }
                    int have = (int)count;

                    int near = 0;
                    if ((*(int *)(slotPtr + 0x24) <= have) && (0 < *(int *)(slotPtr + 0x24))) {
                        near = 1;
                    }
                    int clipMin = *(int *)(slotPtr + 0x24);
                    *(int *)((int)nearOut + slotByteOff) = near;

                    if ((clipMin <= *(short *)(object + ammoField * 2 + 0x61e)) && (0 < clipMin)) {
                        reserveEmpty = true;
                    }

                    if (*(int *)((int)nearOut + slotByteOff) != 0) {
                        if (*(int *)(object + 0x94) == 0x23) {
                            // grenade: fuse gate
                            int fuse = FUN_801b7e74();
                            int fired;
                            if ((*(int *)(object + 0xbac) < 1) ||
                                (fired = FUN_801b7e08(fuse - *(int *)(object + 0xbac), budget),
                                 fired == 0)) {
                                *(int *)((int)fireOut   + slotByteOff) = 0;
                                *(int *)((int)refillOut + slotByteOff) = 0;
                            } else {
                                *(int *)((int)fireOut   + slotByteOff) = 1;
                                *(int *)((int)refillOut + slotByteOff) = 1;
                            }
                        } else {
                            // fire-rate ramped travel-time test against budget.
                            // LUT read: base DAT_804308c4 + object[+0x204]*0x24 + ((clip-1)/5)*4 bytes.
                            float rate;
                            int rateByteOff = *(int *)(object + 0x204) * 0x24 +
                                              ((*(int *)(object + 0xb04) - 1) / 5) * 4;
                            float rateBase = *(float *)((int)&DAT_804308c4 + rateByteOff);
                            if (DAT_805015bd == '\n') {
                                rate = rateBase;
                            } else {
                                rate = FLOAT_80616c0c *
                                       ((float)((int)*(unsigned int *)(object + 0xb04)) / FLOAT_80616c74) +
                                       rateBase;
                                if ((*(unsigned int *)(object + 0xc6c) & 0x1000) != 0) {
                                    rate = rate * FLOAT_80616bfc;
                                }
                            }

                            float dt = (float)((int)DAT_806109c0);

                            // slot +0x10 velocity term -> travel distance this frame (near)
                            int distNear = (int)(rate * (float)((int)(*(float *)(slotPtr + 0x10) * dt)) +
                                                 *(float *)(slotPtr + 0xc) * dt);
                            if ((distNear <= (int)budget) &&
                                (*(int *)(resultOut + 0x20) = 1, *(int *)(slotPtr + 0x24) <= have) &&
                                (*(int *)(slotPtr + 4) != -1)) {
                                *(int *)((int)fireOut + slotByteOff) = 1;
                            }

                            // slot +0x14 velocity term -> travel distance this frame (refill)
                            int distRefill = (int)(rate * (float)((int)(*(float *)(slotPtr + 0x14) * dt)) +
                                                   *(float *)(slotPtr + 0xc) * dt);
                            if ((distRefill <= (int)budget) &&
                                (*(int *)(resultOut + 0x20) = 1, *(int *)(slotPtr + 0x28) <= have) &&
                                (*(int *)(slotPtr + 4) != -1)) {
                                *(int *)((int)refillOut + slotByteOff) = 1;
                            }

                            if (*(int *)(object + 0x94) == 0x10) {
                                // charge weapon: mirror slots by charge level
                                if (*(int *)(object + 0xbac) < 0x15) {
                                    *(int *)((int)refillOut + slotByteOff) =
                                        *(int *)((int)fireOut + slotByteOff);
                                } else {
                                    *(int *)((int)fireOut + slotByteOff) =
                                        *(int *)((int)refillOut + slotByteOff);
                                }
                            }
                        }
                    }
                }
                slotByteOff += 4;
            }
        }
        handByteOff += 8;
    }

    // Fold the per-hand scratch into the result block at resultOut (2 slots).
    int off = 0;
    for (int i = 2; i != 0; i--) {
        unsigned int *out = (unsigned int *)(resultOut + off);
        int fireA = *(int *)((int)local_a8 + off + 0x20);
        out[0] = 1;
        if (fireA == 0) {
            out[4] = *(unsigned int *)((int)local_a8 + off + 0x28);
        } else {
            int refillA = *(int *)((int)local_a8 + off + 0x28);
            out[4] = 1;
            if (refillA == 0) {
                out[0] = 0;
            } else {
                out[0] = useAlt;
            }
        }
        out[2] = 1;
        if (*(int *)((int)local_a8 + off + 0x10) == 0) {
            out[6] = *(unsigned int *)((int)local_a8 + off + 0x18);
        } else {
            int refillB = *(int *)((int)local_a8 + off + 0x18);
            out[6] = 1;
            if (refillB == 0) {
                out[2] = 0;
            } else {
                out[2] = useAlt;
            }
        }
        unsigned int both = 0;
        if ((*(int *)((int)local_a8 + off) == 0) && (*(int *)((int)local_a8 + off + 8) == 0)) {
            both = 1;
        }
        out[9] = both;
        off += 4;
    }

    unsigned int canFire = 0;
    if (((*(int *)(resultOut + 0x24) != 0) && (*(int *)(resultOut + 0x28) != 0)) && (!reserveEmpty)) {
        canFire = 1;
    }
    *(int *)(resultOut + 0x2c) = canFire;

    FUN_80382700();
}

// ===========================================================================
// Address: 0x801b6afc | Name: object_weapon_return_clip_to_reserve | Date: 2026-09-06
// ---------------------------------------------------------------------------
// On unequip/holster, folds the two ammo fields of the weapon back from the
// per-clip counter (weaponBlock +0x38) into the object's reserve pool
// (object +0x61e), then zeroes the clip. Reads which ammo fields to move from
// the weapon-type descriptor (+0x14 primary, +0x1c secondary).
// ===========================================================================
void object_weapon_return_clip_to_reserve(int object, int weaponBlock)
{
    if (*(int *)(weaponBlock + 0x30) == -1) {
        return;
    }
    int descBase = DAT_80610ba0 + *(int *)(weaponBlock + 0x30) * 0x29c;

    if (*(int *)(descBase + 0x14) != 0) {
        int fieldOff = *(int *)(descBase + 0x14) * 2;
        *(short *)(object + fieldOff + 0x61e) =
            (short)(*(short *)(object + fieldOff + 0x61e) + *(short *)(weaponBlock + fieldOff + 0x38));
        *(short *)(weaponBlock + *(int *)(descBase + 0x14) * 2 + 0x38) = 0;
    }
    if (*(int *)(descBase + 0x1c) == 0) {
        return;
    }
    int fieldOff2 = *(int *)(descBase + 0x1c) * 2;
    *(short *)(object + fieldOff2 + 0x61e) =
        (short)(*(short *)(object + fieldOff2 + 0x61e) + *(short *)(weaponBlock + fieldOff2 + 0x38));
    *(short *)(weaponBlock + *(int *)(descBase + 0x1c) * 2 + 0x38) = 0;
}

// ===========================================================================
// Address: 0x801b6b90 | Name: object_weapon_teardown | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon teardown for a player object: fade out the two looping firing-sound
// handles (+0x69 and +0xb5 words), then dispatch the per-hand unequip
// (object_type_dispatch op 6) for the primary (+0x184), secondary (+0x4e4) and
// tertiary (+0x844) weapon blocks of the linked object at *(object+0x150).
// ===========================================================================
void object_weapon_teardown(int *object)
{
    int linked = *(int *)(*object + 0x150);

    if (-1 < object[0x69]) {
        sound_register_fade_out((double)FLOAT_80616ba8, (double)FLOAT_80616c00, object + 0x69, (unsigned int *)0);
    }
    if (-1 < object[0xb5]) {
        sound_register_fade_out((double)FLOAT_80616ba8, (double)FLOAT_80616c00, object + 0xb5, (unsigned int *)0);
    }

    // Primary hand (block +0x184).
    int blockP, typeP;
    if (linked == 0) {
        blockP = 0;
        typeP  = 0;
    } else {
        blockP = linked + 0x184;
        typeP  = *(int *)(linked + 0x184 + 0x30);
    }
    if (blockP == 0) {
        typeP = object[0x35];
    }
    object_type_dispatch(6, typeP, blockP, (int)(object + 0x29), 0);

    // Secondary hand (block +0x4e4).
    int blockS, typeS;
    if (linked == 0) {
        blockS = 0;
        typeS  = 0;
    } else {
        blockS = linked + 0x4e4;
        typeS  = *(int *)(linked + 0x4e4 + 0x30);
    }
    if (blockS == 0) {
        typeS = object[0x81];
    }
    object_type_dispatch(6, typeS, blockS, (int)(object + 0x75), 0);

    // Tertiary hand (block +0x844), only when a linked object exists.
    if (linked != 0) {
        int typeT = iRam00000390;
        if (linked != -0x844) {
            typeT = *(int *)(linked + 0x874);
        }
        object_type_dispatch(6, typeT, linked + 0x844, linked + 0xba4, 0);
    }
}

// ===========================================================================
// Address: 0x801b6cf8 | Name: object_weapon_type_field_24c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-type descriptor getter: 32-bit field at +0x24c (or -1 if out of range).
// ===========================================================================
unsigned int object_weapon_type_field_24c(int type)
{
    if ((-1 < type) && (type < 0x42)) {
        return *(unsigned int *)(DAT_80610ba0 + type * 0x29c + 0x24c);
    }
    return 0xffffffff;
}

// ===========================================================================
// Address: 0x801b6d24 | Name: object_weapon_type_field_250 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-type descriptor getter: 32-bit field at +0x250 (or -1 if out of range).
// ===========================================================================
unsigned int object_weapon_type_field_250(int type)
{
    if ((-1 < type) && (type < 0x42)) {
        return *(unsigned int *)(DAT_80610ba0 + type * 0x29c + 0x250);
    }
    return 0xffffffff;
}

// ===========================================================================
// Address: 0x801b6d50 | Name: object_weapon_type_scale_34 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-type descriptor float getter at +0x34 (returned as double); defaults
// to 1.0 for out-of-range / unset (type == -1) ids.
// ===========================================================================
double object_weapon_type_scale_34(int type)
{
    if ((-1 < type) && (type < 0x42)) {
        return (double)*(float *)(DAT_80610ba0 + type * 0x29c + 0x34);
    }
    return (double)FLOAT_80616ba8;
}

// ===========================================================================
// Address: 0x801b6d8c | Name: object_weapon_type_scale_38 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Weapon-type descriptor float getter at +0x38 (returned as double); defaults
// to 1.0 for out-of-range / unset ids.
// ===========================================================================
double object_weapon_type_scale_38(int type)
{
    if ((-1 < type) && (type < 0x42)) {
        return (double)*(float *)(DAT_80610ba0 + type * 0x29c + 0x38);
    }
    return (double)FLOAT_80616ba8;
}

// ===========================================================================
// Address: 0x801b6dc8 | Name: object_weapon_type_indexed_field_14 | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Indexed weapon-type getter: maps object-type id through the DAT_8043e23c
// index table (stride 0xf) to a descriptor id, then reads the 32-bit field at
// +0x14 (0 if any bound fails).
// ===========================================================================
unsigned int object_weapon_type_indexed_field_14(int type)
{
    int idx;
    if (((-1 < type) && (type < 0x46)) &&
        ((idx = DAT_8043e23c[type * 0xf], -1 < idx) && (idx < 0x42))) {
        return *(unsigned int *)(DAT_80610ba0 + idx * 0x29c + 0x14);
    }
    return 0;
}

// ===========================================================================
// Address: 0x801b6e18 | Name: object_weapon_type_indexed_field_1c | Date: 2026-09-06
// ---------------------------------------------------------------------------
// Indexed weapon-type getter (as above) reading the 32-bit field at +0x1c.
// ===========================================================================
unsigned int object_weapon_type_indexed_field_1c(int type)
{
    int idx;
    if (((-1 < type) && (type < 0x46)) &&
        ((idx = DAT_8043e23c[type * 0xf], -1 < idx) && (idx < 0x42))) {
        return *(unsigned int *)(DAT_80610ba0 + idx * 0x29c + 0x1c);
    }
    return 0;
}
