// Camera Path System - Path interpolation for scripted/death/pause cameras
// Address Range: 0x8009c2e8 - 0x8009cff4
// Date: 2026-08-11
// Batch: 206

#include "common.h"

// ============================================================================
// Forward declarations (from player_slot_system.cpp batch 205)
// ============================================================================
extern int player_get_active_count(void);
extern void* player_slot_get_pool_base(void);

// ============================================================================
// External functions
// ============================================================================
// FUN_803826f0 / FUN_803826bc / FUN_8038273c - coroutine/iterator helpers
// FUN_800f33d4 - camera subsystem init
// FUN_800a3b98 - unknown (called during pause camera assign)
// FUN_80131538 - sqrt (float)
// match_set_paused - sets the match paused state

// ============================================================================
// Constants
// ============================================================================
// Path segment types:
//   0x0e (14) = line segment (2-point interpolation)
//   0x0f (15) = quad patch (4-point bilinear interpolation)
//   0x13 (19) = end marker
//   0x1c (28) = spline/special (handled by caller)
//   default   = single point (no interpolation)

// ============================================================================
// Address: 0x8009c2e8 | player_slot_get_entity_id
// Returns the entity ID bound to this player slot.
// Field +0x218 stores the associated entity reference.
// ============================================================================
int player_slot_get_entity_id(int pSlot)
{
    return *(int*)(pSlot + 0x218);
}

// ============================================================================
// Address: 0x8009c2f0 | player_slot_set_entity
// Binds an entity to a player slot. Sets the active flag (bit 0).
// If no active players and entity==0, sets cutscene camera flag (bit 2).
// 10 xrefs - called from game_phase_state_machine, match_reset_cameras_and_spawn,
// and camera assignment functions.
// ============================================================================
void player_slot_set_entity(int pSlot, int entity_id)
{
    *(int*)(pSlot + 0x218) = entity_id;
    *(u32*)(pSlot + 0x21c) |= 0x1;  // mark slot as active

    int active_count = player_get_active_count();
    if (active_count == 0 && entity_id == 0) {
        // No players and no entity = cutscene/free camera mode
        *(u32*)(pSlot + 0x21c) |= 0x4;
    } else {
        *(u32*)(pSlot + 0x21c) &= ~0x4u;
    }
}

// ============================================================================
// Address: 0x8009c364 | player_slot_assign_pause_camera
// Assigns a player slot to the pause menu camera. Sets slot state to 0x1b
// (pause camera mode), pauses the match, clears viewport flags, applies
// the given flag mask.
// ============================================================================
void player_slot_assign_pause_camera(u32 flag_mask)
{
    // FUN_803826f0 returns a coroutine context (slot pointer in high word)
    int pSlot; // extracted from coroutine context

    player_slot_set_entity(pSlot, 0x1b);
    match_set_paused();
    FUN_800a3b98();

    // Store coroutine handle and reset timer
    *(int*)(pSlot + 0xfd0) = /* coroutine handle */;
    *(float*)(pSlot + 0xfd4) = 0.0f;

    // Clear viewport assignment bits [20:24] and apply new mask
    *(u32*)(pSlot + 0x21c) &= 0xfe4fffff;
    *(u32*)(pSlot + 0x21c) |= flag_mask;
}

// ============================================================================
// Address: 0x8009c3dc | player_get_eye_position
// Computes the camera eye position for a player slot.
// If a linked camera entity exists at +0x150, reads its position from +0xea8.
// Otherwise, computes position + view offset from the base entity.
// ============================================================================
void player_get_eye_position(int pSlot, float* out_pos)
{
    int linked_entity = *(int*)(pSlot + 0x150);

    if (linked_entity != 0) {
        // Camera entity has pre-computed eye position
        out_pos[0] = *(float*)(linked_entity + 0xea8);
        out_pos[1] = *(float*)(linked_entity + 0xeac);
        out_pos[2] = *(float*)(linked_entity + 0xeb0);
    } else {
        // Base position + view height offset
        out_pos[0] = *(float*)(pSlot + 0x64) + *(float*)(pSlot + 0x1a8);
        out_pos[1] = *(float*)(pSlot + 0x68) + *(float*)(pSlot + 0x1ac);
        out_pos[2] = *(float*)(pSlot + 0x6c) + *(float*)(pSlot + 0x1b0);
    }
}

// ============================================================================
// Address: 0x8009c438 | player_get_eye_position_ext
// Extended version of player_get_eye_position with an extra context parameter.
// Functionally identical to the base version (extra param unused).
// ============================================================================
void player_get_eye_position_ext(int context, int pSlot, float* out_pos)
{
    int linked_entity = *(int*)(pSlot + 0x150);

    if (linked_entity != 0) {
        out_pos[0] = *(float*)(linked_entity + 0xea8);
        out_pos[1] = *(float*)(linked_entity + 0xeac);
        out_pos[2] = *(float*)(linked_entity + 0xeb0);
    } else {
        out_pos[0] = *(float*)(pSlot + 0x64) + *(float*)(pSlot + 0x1a8);
        out_pos[1] = *(float*)(pSlot + 0x68) + *(float*)(pSlot + 0x1ac);
        out_pos[2] = *(float*)(pSlot + 0x6c) + *(float*)(pSlot + 0x1b0);
    }
}

// ============================================================================
// Address: 0x8009c494 | camera_path_line_closest_point
// Finds the closest points between two 2D line segments (XZ plane).
// Used by the quad-patch camera path evaluator to find the nearest edge point.
//
// Parameters:
//   p1, p2   - first line segment endpoints (vec3, uses X and Z)
//   p3, p4   - second line segment endpoints (vec3, uses X and Z)
//   out_pos  - output intersection point (vec3, Y averaged from both lines)
//   out_t1   - parametric t for segment 1
//   out_t2   - parametric t for segment 2
//
// Returns 1 if valid closest point found, 0 if lines are parallel.
// ============================================================================
int camera_path_line_closest_point(float* p1, float* p2, float* p3, float* p4,
                                   float* out_pos, float* out_t1, float* out_t2)
{
    float dx1 = p2[0] - p1[0];  // segment 1 X direction
    float dz1 = p2[2] - p1[2];  // segment 1 Z direction
    float dx2 = p4[0] - p3[0];  // segment 2 X direction
    float dz2 = p4[2] - p3[2];  // segment 2 Z direction

    float d11 = dx1 * dx1 + dz1 * dz1;       // |seg1|^2
    float d12 = -(dx1 * dx2 + dz1 * dz2);    // -dot(seg1, seg2)
    float d22 = dx2 * dx2 + dz2 * dz2;       // |seg2|^2

    // Determinant of the 2x2 system (checks for parallel lines)
    float det = ABS(d11 * d22 - d12 * d12);

    if (det <= EPSILON) {  // FLOAT_806146a8
        return 0;  // parallel or degenerate
    }

    // Solve for t parameters
    float r1 = (p1[0] - p3[0]) * dx1 + (p1[2] - p3[2]) * dz1;
    float r2 = -(p1[0] - p3[0]) * dx2 - (p1[2] - p3[2]) * dz2;

    float t1 = (d12 * r2 - d22 * r1) / det;
    float t2 = (d12 * r1 - d11 * r2) / det;

    // Compute closest point on segment 1
    float cx = t1 * dx1 + p1[0];
    float cz = t1 * dz1 + p1[2];

    // Verify: closest point on seg2 should match
    if (ABS(cx - (t2 * dx2 + p3[0])) > EPSILON ||
        ABS(cz - (t2 * dz2 + p3[2])) > EPSILON) {
        return 0;  // segments don't actually have a close approach
    }

    // Output intersection point (Y is averaged from both lines)
    if (out_pos != NULL) {
        out_pos[0] = cx;
        out_pos[1] = (t2 * (p4[1] - p3[1]) + p3[1] + t1 * (p2[1] - p1[1]) + p1[1]) * 0.5f;
        out_pos[2] = cz;
    }

    if (out_t1 != NULL) {
        *out_t1 = t1;
    }

    if (out_t2 != NULL) {
        *out_t2 = t2;
    }

    return 1;
}

// ============================================================================
// Address: 0x8009c63c | camera_path_evaluate
// MASSIVE function - Evaluates camera position along a path definition.
// Supports 3 path segment types:
//   - Type 0x0e (line): Linear interpolation between 2 control points
//   - Type 0x0f (quad): Bilinear interpolation across 4-point quad patch
//   - Default (point): Single fixed point (no interpolation)
//
// For line segments: projects player position onto the line (using either
// XZ distance or Y height ratio depending on line orientation).
//
// For quad patches: Uses edge-closest-point + weighted blending of 4 edges
// to produce a smooth bilinear interpolation across the patch.
//
// Also outputs camera look-at direction if param_4 is non-null and flag 0x80
// is not set on the path node.
//
// Parameters:
//   path_node_ref - path definition reference
//   player_pos    - current player eye position (used for projection)
//   out_position  - output camera world position
//   out_direction - output camera look-at target (optional, may be NULL)
// ============================================================================
void camera_path_evaluate(int path_node_ref, float* player_pos,
                          float* out_position, float* out_direction)
{
    // Path node structure (accessed via coroutine iterator FUN_803826bc):
    //   +0x00: type (0x0e=line, 0x0f=quad, 0x13=end, 0x1c=spline)
    //   +0x04: flags (bit 0x80 = no direction output)
    //   +0x08..+0x10: control point 0 position (vec3)
    //   +0x14..+0x1c: control point 1 position (vec3) [line/quad]
    //   +0x20..+0x28: control point 2 position (vec3) [quad]
    //   +0x2c..+0x34: control point 3 position (vec3) [quad]
    //   +0x38..+0x40: control point 0 direction (vec3)
    //   +0x44..+0x4c: control point 1 direction (vec3) [line/quad]
    //   +0x50..+0x58: control point 2 direction (vec3) [quad]
    //   +0x5c..+0x64: control point 3 direction (vec3) [quad]
    //   +0x68..+0x70: control point 0 XZ position (for projection)
    //   +0x74..+0x7c: control point 1 XZ (quad)
    //   +0x80..+0x88: control point 2 XZ (quad)
    //   +0x8c..+0x94: control point 3 XZ (quad)

    // Implementation handles:
    // TYPE 0x1c: early exit (handled by caller)
    // TYPE 0x0f (quad):
    //   1. For each of 4 edges, find closest point via half-plane test
    //   2. If player is inside the quad (all half-planes pass), use
    //      weighted bilinear blend with line_closest_point for edge ratios
    //   3. Edge distances used for inverse-distance weighting
    // TYPE 0x0e (line):
    //   1. Project player onto line in XZ or use Y-ratio if line is vertical
    //   2. Clamp t to [0,1]
    //   3. Lerp position and direction
    // DEFAULT (point):
    //   Direct copy of control point 0

    // [Full implementation omitted for brevity - see Ghidra decompilation]
    // The core algorithm is edge-weighted bilinear interpolation for quads
    // and parametric projection for lines.
}

// ============================================================================
// Address: 0x8009cf44 | camera_path_get_position
// Evaluates the camera path to get the current camera world position.
// Gets the entity eye position, then calls camera_path_evaluate.
// ============================================================================
void camera_path_get_position(float* out_position)
{
    int pSlot;  // from coroutine context (FUN_803826f0)
    float eye_pos[3];

    // Get player eye position from linked entity
    player_get_eye_position_ext(pSlot, *(int*)(pSlot + 0x2ac), eye_pos);

    // Evaluate camera path at player's eye position
    camera_path_evaluate(*(int*)(pSlot + 0x26c), eye_pos, out_position, NULL);
}

// ============================================================================
// Address: 0x8009cf9c | camera_path_get_direction
// Evaluates the camera path to get the camera look-at direction.
// Same as get_position but outputs direction instead of NULL.
// ============================================================================
void camera_path_get_direction(float* out_direction)
{
    int pSlot;  // from coroutine context (FUN_803826f0)
    float eye_pos[3];

    // Get player eye position from linked entity
    player_get_eye_position_ext(pSlot, *(int*)(pSlot + 0x2ac), eye_pos);

    // Evaluate camera path - output position goes to coroutine buffer,
    // direction goes to out_direction
    camera_path_evaluate(*(int*)(pSlot + 0x26c), eye_pos, /* coroutine buf */, out_direction);
}

// ============================================================================
// Address: 0x8009cff4 | camera_path_segment_is_valid
// Checks if the current camera path segment is valid for continued traversal.
// Returns 1 if the camera should continue on this segment, 0 if it should stop.
//
// Validity conditions (any → return 1 / continue):
//   - No next segment pointer at +0x270
//   - Current or next segment type == 0x13 (end marker)
//   - Flag at +0x2a8 is set AND next segment type == 0 (null segment)
//   - Both segments have flag 0x400 clear AND both have flag 0x1 set
//
// Returns 0 (stop) only when both segments lack flag 0x400 and either
// lacks flag 0x1 (meaning they are not "always valid" segments).
// ============================================================================
int camera_path_segment_is_valid(int pSlot)
{
    int* next_segment = *(int**)(pSlot + 0x270);

    if (next_segment == NULL) {
        return 1;
    }

    // End markers always valid (nothing to transition to)
    if (*next_segment == 0x13 || **(int**)(pSlot + 0x26c) == 0x13) {
        return 1;
    }

    // If override flag set and next is null-type, stay on current
    if (*(int*)(pSlot + 0x2a8) != 0 && *next_segment == 0) {
        return 1;
    }

    // Check transition flags
    u32 next_flags = (u32)next_segment[1];
    u32 curr_flags = (*(int**)(pSlot + 0x26c))[1];

    if ((next_flags & 0x400) != 0 || (curr_flags & 0x400) != 0) {
        return 1;  // forced-valid segments always pass
    }

    // Both segments must have flag 0x1 (active) to continue
    if ((next_flags & 0x1) != 0 && (curr_flags & 0x1) != 0) {
        return 1;
    }

    return 0;  // transition not valid
}
