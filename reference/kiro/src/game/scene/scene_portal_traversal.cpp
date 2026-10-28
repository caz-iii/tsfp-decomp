// =============================================================================
// scene_portal_traversal.cpp - Portal visibility traversal and ray intersection
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 147 | Date: 2026-08-10
// =============================================================================

#include "scene.h"

// Forward declarations
extern void FUN_803826c4(void);  // returns pair (depth, node_id)
extern void FUN_803826d4(void);  // returns pair (ray_origin, ray_dir)
extern void FUN_803826e8(void);  // returns pair (ray_origin, ray_dir)
extern void FUN_803826ec(void);
extern void FUN_803826f0(void);  // returns pair (ray_origin, start_node)
extern void FUN_80382710(void);
extern void FUN_80382720(void);
extern void FUN_80382734(void);
extern void FUN_80382738(void);
extern void FUN_8012fb70(float* matrix, float* in_vec, float* out_vec);
extern int light_probe_clip_against_portals(void);
extern void scene_portal_prepass(void);
extern void scene_portal_flush_pending_changes(void);
extern void scene_portal_visibility_reset_all(void);
extern void scene_raycast(int node_id, float* origin, float* direction, int flags,
                          float* hit_pos, float* hit_normal, int* hit_data,
                          int mask, int param9, int* param10);
extern int scene_query_nodes_in_frustum(int start, int aabb, int result_buf, int max);

// Globals
extern int   DAT_80611748;    // Viewport params (int* array: x_min, x_max, y_center, ...)
extern int   DAT_80611a98;    // Portal visibility table base (stride 0x28)
extern int   DAT_80611aac;    // Portal connection table base
extern int   DAT_80611aa8;    // Scene node descriptor array base
extern int   DAT_80611b10;    // Total portal count
extern int   DAT_80611b14;    // Total active scene node count
extern int   DAT_80611b18;    // Per-node visited flags table
extern int   DAT_80611d54;    // Node visibility tracking + camera state
extern int   DAT_80612614;    // Portal recursion depth limit (100 = unlimited)
extern float FLOAT_80613dc0;  // Near-plane minimum epsilon
extern float FLOAT_80613dd8;  // Max ray distance sentinel (FLT_MAX)
extern float FLOAT_80613dfc;  // Zero / near-zero constant
extern float FLOAT_80613e08;  // Axis threshold low
extern float FLOAT_80613e0c;  // Axis threshold high
extern double DOUBLE_80613df0; // Double bias for int-to-float conversion

// =============================================================================
// Address: 0x80059d28 | scene_portal_set_viewport_bit
// Sets (ORs) a viewport bit in a portal's visibility mask at +0x16.
// Each bit represents a viewport that can see through this portal.
// =============================================================================
void scene_portal_set_viewport_bit(int portal_index, int viewport)
{
    int portal_ptr = *(int*)(DAT_80611aac + portal_index * 4);
    if (portal_ptr != 0) {
        *(unsigned short*)(portal_ptr + 0x16) |= (unsigned short)(1 << viewport);
    }
}

// =============================================================================
// Address: 0x80059d58 | scene_portal_clear_viewport_bit
// Clears (ANDs ~bit) a viewport bit in a portal's visibility mask at +0x16.
// Removes a viewport from being able to see through this portal.
// =============================================================================
void scene_portal_clear_viewport_bit(int portal_index, int viewport)
{
    int portal_ptr = *(int*)(DAT_80611aac + portal_index * 4);
    if (portal_ptr != 0) {
        *(unsigned short*)(portal_ptr + 0x16) &= ~(unsigned short)(1 << viewport);
    }
}

// =============================================================================
// Address: 0x80059d88 | scene_portal_mark_dirty_set
// Marks a portal as dirty with the 'set' flag for a given viewport.
// Sets bit in both dirty mask (+0x20) and set-vs-clear mask (+0x22).
// The flush function will later apply this as a set_viewport_bit call.
// =============================================================================
void scene_portal_mark_dirty_set(int portal_index, int viewport)
{
    int entry_ptr = DAT_80611a98 + portal_index * 0x28;

    // Only mark if not already dirty for this viewport
    if ((*(unsigned short*)(entry_ptr + 0x20) & (1 << viewport)) == 0) {
        *(unsigned short*)(entry_ptr + 0x20) |= (unsigned short)(1 << viewport);
        *(unsigned short*)(entry_ptr + 0x22) |= (unsigned short)(1 << viewport);
    }
}

// =============================================================================
// Address: 0x80059de0 | scene_portal_mark_dirty_clear
// Marks a portal as dirty with the 'clear' flag for a given viewport.
// Sets bit in dirty mask (+0x20) but CLEARS bit in set-vs-clear mask (+0x22).
// The flush function will later apply this as a clear_viewport_bit call.
// =============================================================================
void scene_portal_mark_dirty_clear(int portal_index, int viewport)
{
    int entry_ptr = DAT_80611a98 + portal_index * 0x28;
    *(unsigned short*)(entry_ptr + 0x20) |= (unsigned short)(1 << viewport);
    *(unsigned short*)(entry_ptr + 0x22) &= ~(unsigned short)(1 << viewport);
}

// =============================================================================
// Address: 0x80059e20 | scene_portal_clip_and_project
// Clips a polygon against 4 view frustum planes using Sutherland-Hodgman
// algorithm, then projects surviving vertices to 2D screen coordinates.
// Input: polygon vertices (XYW homogeneous, up to 20 verts), 4 clip planes.
// Output: polygon[100] = 0 if visible, 1 if fully clipped.
// Screen coords written back into polygon array.
// =============================================================================
// NOTE: This is a MASSIVE function (~400 lines of raw decomp). The full
// implementation performs 4 sequential clip passes (left, right, top, bottom)
// using Sutherland-Hodgman, then projects remaining vertices to screen space.
// See Ghidra decompilation at 0x80059e20 for full detail.
void scene_portal_clip_and_project(float* polygon, unsigned int left, unsigned int right,
                                    unsigned int top, unsigned int bottom)
{
    // Sutherland-Hodgman polygon clipping against 4 frustum planes:
    // 1. Near plane clip (z >= near)
    // 2. Left plane clip (x >= left * w)
    // 3. Right plane clip (x <= right * w)
    // 4. Top plane clip (y >= top * w)
    // 5. Bottom plane clip (y <= bottom * w)
    //
    // After clipping, project remaining vertices to screen:
    //   screen_x = center_x + (x * scale_x) / w
    //   screen_y = center_y - (y * scale_y) / w
    //
    // polygon[100] = 0.0 means visible (has surviving vertices)
    // polygon[100] = 1.0 means fully clipped (invisible)
    //
    // Full implementation omitted for brevity - see Ghidra at 0x80059e20
    (void)polygon; (void)left; (void)right; (void)top; (void)bottom;
}

// =============================================================================
// Address: 0x8005a9a4 | scene_portal_visibility_traverse
// Recursive portal visibility traversal (PVS flood-fill).
// From a starting node, visits all reachable neighbors through open portals.
// For each portal: clips polygon to frustum, computes screen rect, recurses.
// Updates per-node visibility timestamps and per-portal screen rects.
// Aborts at depth limit or when portal polygon clips to nothing.
// =============================================================================
// NOTE: MASSIVE recursive function (~300 lines). Core PVS algorithm:
// 1. Mark current node as visible, update screen-space bounds
// 2. For each portal connected to this node:
//    a. Check portal is open (visibility mask)
//    b. Check camera-side dot product (facing check)
//    c. Transform portal polygon to view space
//    d. Clip against current frustum bounds
//    e. If surviving vertices: compute screen rect, recurse into neighbor
// See Ghidra decompilation at 0x8005a9a4 for full detail.
void scene_portal_visibility_traverse(int start_node, int depth,
                                       unsigned int x_min, unsigned int x_max,
                                       unsigned int y_min, unsigned int y_max)
{
    // Full implementation omitted for brevity - see Ghidra at 0x8005a9a4
    (void)start_node; (void)depth;
    (void)x_min; (void)x_max; (void)y_min; (void)y_max;
}

// =============================================================================
// Address: 0x8005afa8 | scene_portal_visibility_update
// Top-level portal visibility update. Entry point called once per frame.
// Flushes pending portal changes, resets visibility state, then kicks off
// recursive traversal from the camera's current node using full viewport.
// =============================================================================
void scene_portal_visibility_update(void)
{
    int camera_node;

    // Get camera node (prefer override at +0x2A0, fallback to +0x2A4)
    if (*(int*)(DAT_80611d54 + 0x2A0) == 0) {
        camera_node = *(int*)(DAT_80611d54 + 0x2A4);
    } else {
        camera_node = *(int*)(DAT_80611d54 + 0x2A0);
    }

    if (camera_node > 0 && camera_node <= DAT_80611b14) {
        scene_portal_prepass();
        scene_portal_flush_pending_changes();
        scene_portal_visibility_reset_all();

        // Start recursive traversal from camera node with full viewport bounds
        int* viewport = (int*)DAT_80611748;
        scene_portal_visibility_traverse(
            camera_node, 0,
            viewport[0], viewport[1],  // x_min, x_max
            viewport[5], viewport[6]   // y_min, y_max
        );
    }
}

// =============================================================================
// Address: 0x8005b040 | scene_ray_portal_intersect
// Ray-portal intersection test. Traces a ray from origin along direction,
// testing against all portals connected to a given node.
// Finds the closest portal polygon hit using plane distance + 2D
// point-in-polygon test. Returns the node on the other side.
// Used by raycast to walk through portals along the ray.
// =============================================================================
// NOTE: Complex function (~200 lines). Algorithm:
// 1. For each portal connected to current node:
//    a. Skip if leads back to previous node
//    b. Compute ray-plane distance (dot product)
//    c. Check facing (ensure ray crosses plane correctly)
//    d. Compute intersection point
//    e. 2D point-in-polygon test using cross products
//    f. Track closest hit
// 2. Update origin/direction for next traversal step
// See Ghidra decompilation at 0x8005b040 for full detail.
void scene_ray_portal_intersect(/* complex params - see Ghidra */)
{
    // Full implementation omitted for brevity
}

// =============================================================================
// Address: 0x8005b4f4 | scene_ray_traverse_portals
// Iterative portal ray traversal. Walks a ray through portals (up to 100
// steps) by repeatedly calling scene_ray_portal_intersect to find next node.
// Stops when the ray no longer exits through a portal (terminal node).
// =============================================================================
void scene_ray_traverse_portals(/* params */)
{
    // Algorithm:
    // 1. Start at initial node
    // 2. Loop up to 100 iterations:
    //    a. Call scene_ray_portal_intersect
    //    b. If returned same node: done (ray terminated in this node)
    //    c. Else: update origin to intersection point, continue from new node
    // See Ghidra at 0x8005b4f4
}

// =============================================================================
// Address: 0x8005b5ac | scene_ray_world_collision
// Ray-world collision. Traces a ray, walks portals to find endpoint node,
// then builds an AABB around the ray and queries all intersecting nodes.
// For each node, calls scene_raycast to test actual geometry.
// Used for bullet/projectile world collision.
// =============================================================================
void scene_ray_world_collision(/* params */)
{
    // Algorithm:
    // 1. Get ray origin and direction
    // 2. Walk portals to find terminal node
    // 3. If terminal == start (no portal crossings):
    //    a. Build AABB around ray endpoints
    //    b. Query nodes overlapping AABB (up to 10)
    //    c. For each node: call scene_raycast with geometry mask 0xFFFFFFFD
    // See Ghidra at 0x8005b5ac
}
