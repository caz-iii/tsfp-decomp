// Address: 0x80086804 - 0x80086c1c | Batch 181-182 | Date: 2026-08-11
// Trigger operations: open/close commands, slot lookup, state queries, destruction
// Part of the trigger/door system (continuation of trigger_state.cpp)

#include "trigger.h"
#include "mover.h"
#include "entity.h"
#include "scene_portal.h"

// ============================================================================
// Globals (defined in trigger_system.cpp)
// ============================================================================
extern float FLOAT_80614294;  // 1.0f - fully open position
extern float FLOAT_80614280;  // 0.0f - fully closed position

extern int DAT_80611ca0;      // Trigger slot array base pointer
extern int DAT_80611ca4;      // Trigger slot count
extern int DAT_80611c24;      // Entity pool base pointer
extern int DAT_80611c10;      // Zone count
extern int DAT_80611b90;      // Zone table base
extern int DAT_80611b8c;      // Zone node table base
extern int DAT_80612614;      // Current level/mode flag

// ============================================================================
// Address: 0x80086804
// trigger_open_full - Opens a trigger to its maximum extent (1.0)
// Wrapper around trigger_open with fixed target position = 1.0
// Called by: trigger_state_machine, trigger_on_spawn, entity_check_vehicle_mount,
//            mover_trigger_auto_return, and many gameplay scripts
// ============================================================================
void trigger_open_full(int pEntity, int param2) {
    trigger_open((double)FLOAT_80614294, pEntity, param2);
}

// ============================================================================
// Address: 0x80086828
// trigger_close_immediate - Force-closes a trigger instantly (no animation)
// Sets state to 2 (closed), position to max, clears speed, flags entity dirty
// Called by: FUN_8022fe70 (likely a scripted trigger reset)
// ============================================================================
void trigger_close_immediate(int pEntity) {
    if (pEntity == 0) {
        return;
    }

    int pTriggerData = *(int *)(pEntity + 0x7c);
    if (pTriggerData == 0) {
        return;
    }

    // Already in state 2 (closed) - nothing to do
    if (*(int *)(pTriggerData + 0x10) == 2) {
        return;
    }

    // Snap position to fully open value (1.0) and set as current
    *(float *)(pTriggerData + 0x74) = FLOAT_80614294;
    *(float *)(pTriggerData + 0x24) = *(float *)(pTriggerData + 0x74);

    // Set state to 2 (closed/arrived)
    *(int *)(pTriggerData + 0x10) = 2;

    // Clear flag 0x04000000 (animation active)
    *(unsigned int *)(pTriggerData + 0xc) &= ~0x04000000u;

    // Reset speed to zero
    *(int *)(pTriggerData + 0x20) = 0;

    // Set interpolation base to 0.0
    *(float *)(pTriggerData + 0x38) = FLOAT_80614280;

    // Mark entity flags dirty (0x42 = needs transform + render update)
    *(unsigned int *)(pEntity + 0x18) |= 0x42;

    // Clear animation-active flag on entity
    *(unsigned int *)(pEntity + 0x28) &= ~0x04000000u;
}

// ============================================================================
// Address: 0x80086898
// trigger_close - Begins closing a trigger with animation and sound
// Sets state to 3 (closing), plays close sound via callback or direct
// Called by: trigger_close_if_idle, trigger_state_machine, mover system,
//            entity_trigger_evaluate_all, and many gameplay scripts
// ============================================================================
void trigger_close(double targetPosition, int pEntity) {
    if (pEntity == 0) {
        return;
    }

    int pTriggerData = *(int *)(pEntity + 0x7c);
    if (pTriggerData == 0) {
        return;
    }

    // If flag 0x1000000 set (locked), cannot close
    if ((*(unsigned int *)(pTriggerData + 0xc) & 0x1000000) != 0) {
        return;
    }

    // Store target position for close animation
    *(float *)(pTriggerData + 0x74) = (float)targetPosition;

    // Don't re-enter closing state if already closing (3) or fully open/resting (0)
    if (*(int *)(pTriggerData + 0x10) == 3 || *(int *)(pTriggerData + 0x10) == 0) {
        return;
    }

    // Set state to 3 (closing)
    *(int *)(pTriggerData + 0x10) = 3;

    // Mark entity dirty
    *(unsigned int *)(pEntity + 0x18) |= 0x40;

    // If has portal flag, mark portal dirty
    if ((*(unsigned int *)(pTriggerData + 0xc) & 0x8000) != 0) {
        *(unsigned int *)(pTriggerData + 0xc) |= 2;
    }

    // Play close sound via callback or direct
    if (*(void **)(pEntity + 0x18c) != (void *)0) {
        // Custom sound callback
        (*(void (**)(int, int))(pEntity + 0x18c))(pEntity, 0);
    } else {
        // Default sound logic
        int closeSoundId = *(int *)(pTriggerData + 0x6c);
        if (closeSoundId >= 0 && (*(unsigned int *)(pTriggerData + 0xc) & 0x4002000) != 0) {
            mover_play_sound_at_entity(pEntity, closeSoundId);
        } else if ((*(unsigned int *)(pTriggerData + 0xc) & 0x300000) != 0) {
            trigger_play_activation_sound();
        } else if (*(int *)(pTriggerData + 0x68) >= 0) {
            mover_play_sound_at_entity(pEntity, *(int *)(pTriggerData + 0x68));
        }
    }
}

// ============================================================================
// Address: 0x800869b4
// trigger_close_if_idle - Closes trigger only if not locked (flag 0x1000)
//                         or not currently in opening state (1)
// Conditional wrapper around trigger_close
// ============================================================================
void trigger_close_if_idle(int pEntity) {
    int pTriggerData = *(int *)(pEntity + 0x7c);

    if ((*(unsigned int *)(pTriggerData + 0xc) & 0x1000) != 0 &&
        *(int *)(pTriggerData + 0x10) == 1) {
        return;
    }

    trigger_close((double)FLOAT_80614280, pEntity);
}

// ============================================================================
// Address: 0x800869f4
// trigger_open_immediate - Force-opens a trigger instantly (no animation)
// Sets state to 0 (open/resting), position to 0, clears speed
// ============================================================================
void trigger_open_immediate(int pEntity) {
    if (pEntity == 0) {
        return;
    }

    int pTriggerData = *(int *)(pEntity + 0x7c);
    if (pTriggerData == 0) {
        return;
    }

    if (*(int *)(pTriggerData + 0x10) == 0) {
        return;
    }

    *(float *)(pTriggerData + 0x74) = FLOAT_80614280;
    *(float *)(pTriggerData + 0x24) = *(float *)(pTriggerData + 0x74);
    *(int *)(pTriggerData + 0x10) = 0;
    *(unsigned int *)(pTriggerData + 0xc) &= ~0x04000000u;
    *(int *)(pTriggerData + 0x20) = 0;
    *(float *)(pTriggerData + 0x38) = FLOAT_80614280;

    if ((*(unsigned int *)(pTriggerData + 0xc) & 0x8000) != 0) {
        *(unsigned int *)(pTriggerData + 0xc) |= 2;
    }

    *(unsigned int *)(pEntity + 0x18) |= 0x42;
    *(unsigned int *)(pEntity + 0x28) &= ~0x04000000u;
}

// ============================================================================
// Address: 0x80086a70
// trigger_slot_find_by_entity_id - Find trigger slot by entity ID
// Searches the 0x98-stride trigger slot array for entity ID match at offset+4
// ============================================================================
int trigger_slot_find_by_entity_id(int entityId) {
    int offset = 0;
    int remaining = DAT_80611ca4;

    if (remaining <= 0) {
        return 0;
    }

    do {
        if (entityId == *(int *)(DAT_80611ca0 + offset + 4)) {
            return DAT_80611ca0 + offset;
        }
        offset += 0x98;
        remaining--;
    } while (remaining != 0);

    return 0;
}

// ============================================================================
// Address: 0x80086ab0
// trigger_slot_resolve_entity - Find entity pointer from trigger slot by ID
// Resolves packed reference (bits 31..22 = pool index) to entity pointer
// Validates via generation counter at entity+0x2c
// ============================================================================
int trigger_slot_resolve_entity(int entityId) {
    int offset = 0;
    int remaining = DAT_80611ca4;

    if (remaining <= 0) {
        return 0;
    }

    do {
        if (entityId == *(int *)(DAT_80611ca0 + offset + 4)) {
            unsigned int packedRef = *(unsigned int *)(DAT_80611ca0 + offset);

            if (packedRef == 0) {
                return 0;
            }

            int pEntity = DAT_80611c24 + (packedRef >> 22) * 0x2e8;

            if (*(unsigned int *)(pEntity + 0x2c) == packedRef) {
                return pEntity;
            }

            *(int *)(DAT_80611ca0 + offset) = 0;
            return 0;
        }
        offset += 0x98;
        remaining--;
    } while (remaining != 0);

    return 0;
}

// ============================================================================
// Address: 0x80086b24
// trigger_toggle_checkpoint_flags - Toggle checkpoint trigger flags
// Iterates trigger slots for CPC magic (0x43504300), toggles bit 2
// ============================================================================
int trigger_toggle_checkpoint_flags(int checkpointIndex) {
    int offset = 0;

    for (int i = 0; i < DAT_80611ca4; i++) {
        int pSlot = DAT_80611ca0 + offset;
        unsigned int entityId = *(unsigned int *)(pSlot + 4);

        if ((entityId & 0xFFFFFF00) == 0x43504300) {
            unsigned int slotIndex = entityId & 0xFF;
            if ((int)slotIndex <= checkpointIndex + 0x30) {
                if (slotIndex > 0x30) {
                    unsigned int flags = *(unsigned int *)(pSlot + 0xc);
                    if ((flags & 2) == 0) {
                        *(unsigned int *)(pSlot + 0xc) = flags | 2;
                    } else {
                        *(unsigned int *)(pSlot + 0xc) = flags & ~2u;
                    }
                }
            }
        }
        offset += 0x98;
    }
    return 0;
}

// ============================================================================
// Address: 0x80086ba0
// trigger_is_moving - Returns whether trigger is currently animating
// Returns 1 if state is 1 (opening) or 2 (closing), 0 otherwise
// ============================================================================
int trigger_is_moving(int pEntity) {
    if (pEntity == 0) {
        return 0;
    }

    int state = *(int *)(*(int *)(pEntity + 0x7c) + 0x10);
    if (state == 1 || state == 2) {
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x80086bd4
// trigger_is_fully_open - Returns whether trigger is in open/resting state
// CLZ(0)=32, 32>>5=1. Returns 1 only when state==0
// ============================================================================
unsigned int trigger_is_fully_open(int pEntity) {
    if (pEntity == 0) {
        return 0;
    }
    unsigned int state = *(unsigned int *)(*(int *)(pEntity + 0x7c) + 0x10);
    return __builtin_clz(state) >> 5;
}

// ============================================================================
// Address: 0x80086bf8
// trigger_is_idle - Returns whether trigger state is 0 (idle/resting)
// Identical logic to trigger_is_fully_open; separate entry point used by
// trigger_tick_update for idle detection in the state machine.
// ============================================================================
unsigned int trigger_is_idle(int pEntity) {
    if (pEntity == 0) {
        return 0;
    }
    unsigned int state = *(unsigned int *)(*(int *)(pEntity + 0x7c) + 0x10);
    return __builtin_clz(state) >> 5;
}

// ============================================================================
// Address: 0x80086c1c
// trigger_destroy_and_launch_entity - Destroys trigger, launches entity
// MASSIVE function: resets trigger to idle state, unblocks portal, clears zone
// flags, computes launch direction from entity matrices and optional overrides,
// then spawns a physics projectile via FUN_8014b028.
// Called by: scripted gameplay (FUN_801ee5e0)
// ============================================================================
void trigger_destroy_and_launch_entity(int param_1, int param_2, int param_3) {
    // Uses FUN_803826ec/FUN_80382738 as iterator begin/end (coroutine-style)
    // Retrieves entity from iterator, checks if timer expired (0x1cc < [8])
    // If expired:
    //   - Resets timer fields to 0
    //   - Clears flag 0x10 on entity+0x28
    //   - Updates parent entity pointer
    //   - Marks dirty (0x42)
    //   - Calls FUN_800ea074 (likely anim/transform reset)
    //   - Clears flags 0xC000 on entity+0x28
    //   - Sets trigger state to 0, locks trigger (flag 0x1000000)
    //   - Unblocks scene portal if closed
    //   - Clears zone flags (0x80000 on zone table, sets 0x800000 on zone nodes)
    //   - Computes launch origin from override position or entity position
    //   - Computes launch direction from:
    //     a) Negated direction vector override (piVar3[0xb])
    //     b) Direction vector override (piVar3[0xc])
    //     c) Normalized vector from origin to entity position (default)
    //   - Transforms direction through entity local matrix
    //   - Applies Z-forward bias (abs(Z) with sign preserved, scaled by 0.7)
    //   - Normalizes final direction
    //   - Inverse-transforms back to world space
    //   - Calls FUN_8014b028 to spawn physics projectile with speed and flags
    // (Full pseudocode omitted for readability - see Ghidra decompilation)
}
