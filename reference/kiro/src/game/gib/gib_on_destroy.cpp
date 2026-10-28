// Address: 0x80098d70 | Name: gib_on_destroy | Date: 2026-08-11
// Gib entity destroy handler. If block type == 5, clears parent reference
// and resets type. Otherwise frees the block.
// Registered from gib_register_event_handlers (0x800988f0).

#include "common.h"

extern void block_free_simple(void* pBlock);

/**
 * gib_on_destroy - Event handler for gib entity destruction.
 *
 * When a gib entity is destroyed, this checks the attached block (offset 0x7c).
 * If the block's type field (offset 0x44) equals 5, it means the block is still
 * referenced by a parent entity (via block->owner->block at 0x68->0x7c). In that
 * case, it clears the parent's back-reference (offset 0xBC4) and resets the block
 * type to 0, effectively detaching it without freeing. Otherwise, it simply frees
 * the block via block_free_simple.
 *
 * @param pEntity Pointer to the gib entity being destroyed
 */
void gib_on_destroy(int pEntity) {
    if (pEntity == 0) {
        return;
    }

    int pBlock = *(int*)(pEntity + 0x7C);

    if (pBlock == 0 || *(int*)(pBlock + 0x44) != 5) {
        // Block is not type-5 (not attached to parent) - just free it
        block_free_simple((void*)pBlock);
    } else {
        // Type-5 block: clear parent's back-reference to this gib
        int pOwnerEntity = *(int*)(*(int*)(pBlock + 0x68) + 0x7C);
        if (pOwnerEntity != 0) {
            *(int*)(pOwnerEntity + 0xBC4) = 0;
        }
        // Reset block type to 0 (detached/available)
        *(int*)(pBlock + 0x44) = 0;
    }
}
