// =============================================================================
// scene_portal_visibility.cpp - Portal visibility state management
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 146 | Date: 2026-08-10
// =============================================================================

#include "scene.h"

// Forward declarations
extern void FUN_803826ec(void);
extern void FUN_80382738(void);
extern void scene_portal_set_viewport_bit(int portal_id, int viewport);
extern void scene_portal_clear_viewport_bit(int portal_id, int viewport);
extern int scene_node_get_base_data(int node_id);

// Globals
extern int DAT_80611a98;   // Portal visibility table base (stride 0x28)
extern int DAT_80611aac;   // Portal connection table base
extern int DAT_80611aa8;   // Scene node descriptor array base
extern int DAT_80611b04;   // Render data pointer table
extern int DAT_80611b10;   // Total portal count
extern int DAT_80611b14;   // Total active scene node count
extern int DAT_80611b18;   // Per-node visibility flags table
extern int DAT_80611d54;   // Node visibility tracking array
extern int DAT_80611d58;   // Active viewport count
extern int DAT_806109c4;   // Default portal node A value
extern int DAT_806109c8;   // Default portal node B value

// Portal visibility table entry (stride 0x28):
//   +0x00: short node_a
//   +0x02: short node_b
//   +0x04: ushort vis_mask_a
//   +0x06: ushort vis_mask_b
//   +0x16: ushort block_mask (0xFFFF = fully blocked)
//   +0x20: ushort dirty_flags
//   +0x22: ushort pending_state
//   +0x24: int reserved

// =============================================================================
// Address: 0x80059ab0 | scene_portal_visibility_reset_all
// Resets all portal and node visibility state for the scene.
// Clears dirty flags, viewport masks, and per-node visibility tracking.
// Called at level load/reset.
// =============================================================================
void scene_portal_visibility_reset_all(void)
{
    // Reset all portal entries
    for (int i = 1; i <= DAT_80611b10; i++) {
        int portal_ptr = DAT_80611a98 + i * 0x28;
        *(short*)(portal_ptr + 0x00) = (short)DAT_806109c4;
        *(short*)(portal_ptr + 0x02) = (short)DAT_806109c8;
        *(unsigned short*)(portal_ptr + 0x04) = 0xFFFF;
        *(unsigned short*)(portal_ptr + 0x06) = 0xFFFF;
        *(int*)(portal_ptr + 0x24) = 0;
    }

    // Reset all node visibility entries
    for (int i = 1; i <= DAT_80611b14; i++) {
        int node_vis_ptr = DAT_80611d54 + i * 0xC;
        *(int*)(node_vis_ptr + 0x3BC) = 0xFFFFFFFF;
        *(short*)(node_vis_ptr + 0x3C0) = (short)DAT_806109c4;
        *(short*)(node_vis_ptr + 0x3C2) = (short)DAT_806109c8;
        *(unsigned short*)(node_vis_ptr + 0x3C4) = 0xFFFF;
        *(unsigned short*)(node_vis_ptr + 0x3C6) = 0xFFFF;
        *(int*)(DAT_80611b18 + i * 4) = 0;
    }

    // Clear terminal sentinel
    *(int*)(DAT_80611d54 + 0xD28) = 0xFFFFFFFF;
}

// =============================================================================
// Address: 0x80059bd4 | scene_portal_flush_pending_changes
// Flushes all pending portal visibility changes.
// Iterates portals with dirty flag (+0x20 != 0), dispatching set/clear
// per viewport bit. Clears dirty flags after processing.
// Called once per frame to batch portal state updates.
// =============================================================================
void scene_portal_flush_pending_changes(void)
{
    FUN_803826ec();

    for (int i = 1; i <= DAT_80611b10; i++) {
        int portal_ptr = DAT_80611a98 + i * 0x28;

        if (*(short*)(portal_ptr + 0x20) != 0) {
            unsigned short dirty_mask = *(unsigned short*)(portal_ptr + 0x20);
            unsigned short pending_state = *(unsigned short*)(portal_ptr + 0x22);

            for (int viewport = 0; viewport < DAT_80611d58; viewport++) {
                if ((dirty_mask & (1 << viewport)) != 0) {
                    if ((pending_state & (1 << viewport)) == 0) {
                        scene_portal_clear_viewport_bit(i, viewport);
                    } else {
                        scene_portal_set_viewport_bit(i, viewport);
                    }
                }
            }

            // Clear dirty state
            *(unsigned short*)(portal_ptr + 0x20) = 0;
            *(unsigned short*)(portal_ptr + 0x22) = 0;
        }
    }

    FUN_80382738();
}

// =============================================================================
// Address: 0x80059cb4 | scene_portal_is_open
// Returns true if a portal is open (not fully blocked).
// A portal is blocked when its visibility mask at +0x16 == 0xFFFF (-1).
// Used by neighbor traversal to skip closed doors/walls.
// =============================================================================
bool scene_portal_is_open(int portal_index)
{
    int portal_ptr = *(int*)(DAT_80611aac + portal_index * 4);
    return *(short*)(portal_ptr + 0x16) != -1;
}

// =============================================================================
// Address: 0x80059ce4 | scene_portal_block
// Blocks a portal by setting its visibility mask to 0xFFFF (fully blocked).
// Prevents traversal between its two connected nodes.
// Used for doors, shutters, or dynamic level geometry changes.
// =============================================================================
void scene_portal_block(int portal_index)
{
    int portal_ptr = *(int*)(DAT_80611aac + portal_index * 4);
    if (portal_ptr != 0) {
        *(unsigned short*)(portal_ptr + 0x16) = 0xFFFF;
    }
}

// =============================================================================
// Address: 0x80059d08 | scene_portal_unblock
// Unblocks a portal by clearing its visibility mask to 0 (fully open).
// Re-enables traversal between the portal's two connected nodes.
// =============================================================================
void scene_portal_unblock(int portal_index)
{
    int portal_ptr = *(int*)(DAT_80611aac + portal_index * 4);
    if (portal_ptr != 0) {
        *(unsigned short*)(portal_ptr + 0x16) = 0;
    }
}
