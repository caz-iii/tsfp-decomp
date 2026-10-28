// =============================================================================
// scene_portal_culling.cpp - Portal culling, raycasting, and priority selection
// TimeSplitters: Future Perfect (GameCube) decompilation
// Batch 148 | Date: 2026-08-10
// =============================================================================

#include "scene.h"
#include <math.h>

// Forward declarations
extern void FUN_803826c0(void);
extern void FUN_803826dc(void);
extern void FUN_803826e0(void);
extern void FUN_803826e8(void);
extern void FUN_8038270c(void);
extern void FUN_8038272c(void);
extern void FUN_80382728(void);
extern void FUN_80382734(void);
extern double FUN_80131538(double value);  // sqrt
extern int FUN_800789f4(int node, float* origin, float* dir, int mode,
                        float* hit_pos, float* hit_normal, int* hit_data, unsigned int mask);
extern int FUN_80078540(int node, float* origin, float* dir, int param4,
                        int* param5, unsigned int param6, unsigned int param7);
extern int FUN_80077e18(int node, float* origin, float* dir, int mode,
                        float* hit_pos, float* hit_normal, unsigned int mask,
                        unsigned int param8, int* param9);
extern int scene_ray_portal_intersect(/* complex params */);
extern int scene_query_nodes_in_frustum(int start, int aabb, int buf, int max);
extern void scene_raycast_full(int mode, int start_node, float* origin, float* dir,
                               int any_hit, float* hit_pos, float* hit_normal,
                               int* hit_node, void* extra, unsigned int mask,
                               unsigned int param11, int* param12);
extern void scene_room_streaming_update(/* passthrough */);
extern void scene_streaming_update(/* passthrough */);
extern void scene_update_slot_visibility(void);

// Globals
extern int   DAT_80611aac;     // Portal connection table base
extern int   DAT_80611d54;     // Node visibility tracking + camera state
extern float FLOAT_80613db0;   // 1.0f constant
extern float FLOAT_80613dc0;   // Near-plane epsilon
extern float FLOAT_80613dd8;   // FLT_MAX sentinel
extern float FLOAT_80613ddc;   // Min ray length squared
extern float FLOAT_80613dfc;   // Zero constant
extern float FLOAT_80613de0;   // 0.5f (triangle area factor)
extern float FLOAT_80613e10;   // Epsilon for normalize check
extern float FLOAT_80613e14;   // -0.5f (Newton-Raphson)
extern float FLOAT_80613e18;   // -1.5f (Newton-Raphson)
extern float FLOAT_80613e1c;   // 1.0f (scale factor)
extern float FLOAT_80613e20;   // -1.0f (back-face scale)
extern float FLOAT_80613e24;   // Plane test epsilon
extern float DAT_803cc1e8;     // Default hit position X
extern float DAT_803cc1ec;     // Default hit position Y
extern float DAT_803cc1f0;     // Default hit position Z
extern float DAT_803cc1f4;     // Default hit normal X
extern float DAT_803cc1f8;     // Default hit normal Y
extern float DAT_803cc1fc;     // Default hit normal Z
extern int   DAT_8052d948;     // Portal priority queue count array
extern int   DAT_8052d964;     // Portal priority queue area array
extern int   DAT_8052d608;     // Portal priority queue node buffer

// =============================================================================
// Address: 0x8005b808 | scene_raycast_full
// Full ray-scene intersection with portal walking.
// Tests geometry in each node along the ray path. Finds closest hit.
// mode param_5: 0 = closest hit only, nonzero = first any-hit.
// Walks portals via scene_ray_portal_intersect until ray consumed.
// NOTE: MASSIVE function - see Ghidra for full implementation.
// =============================================================================
void scene_raycast_full(int mode, int start_node, float* origin, float* direction,
                        int any_hit_mode, float* out_hit_pos, float* out_hit_normal,
                        int* out_hit_node, void* out_extra,
                        unsigned int mask, unsigned int param11, int* param12)
{
    // Algorithm:
    // 1. Initialize with default hit values (miss)
    // 2. Compute ray length squared; abort if too short
    // 3. Set max distance: closest-hit uses ray length, any-hit uses FLT_MAX
    // 4. Loop: test geometry in current node
    //    a. Call node-specific raycast (FUN_800789f4 or FUN_80077e18)
    //    b. If hit found closer than current best: update best hit
    //    c. Walk to next node via scene_ray_portal_intersect
    //    d. Stop when ray origin exceeds max distance
    // 5. Write results: hit position, normalized normal, hit node
    // See Ghidra at 0x8005b808
    (void)mode; (void)start_node; (void)origin; (void)direction;
    (void)any_hit_mode; (void)out_hit_pos; (void)out_hit_normal;
    (void)out_hit_node; (void)out_extra; (void)mask; (void)param11; (void)param12;
}

// =============================================================================
// Address: 0x8005bb44 | scene_raycast
// Simplified raycast entry point. Wraps scene_raycast_full with mode=1
// (any-hit) and allocates temp buffer for internal state.
// =============================================================================
void scene_raycast(int node_id, float* origin, float* direction, int mode,
                   float* hit_pos, float* hit_normal, int* hit_node,
                   unsigned int mask, unsigned int param9, int* param10)
{
    // FUN_803826e8 returns (start_node, origin_ptr)
    FUN_803826e8();
    // Delegates to scene_raycast_full with mode=1
    scene_raycast_full(1, /* start_node */, /* origin */, direction,
                       mode, hit_pos, hit_normal, hit_node, NULL, mask, param9, param10);
    FUN_80382734();
}

// =============================================================================
// Address: 0x8005bbcc | scene_swept_aabb_query
// Swept AABB scene query. Builds expanded bounding box around
// origin+direction, padded by radius, queries all intersecting nodes.
// For each node, tests geometry against the swept volume.
// Used for thick/capsule raycasts and movement collision.
// =============================================================================
void scene_swept_aabb_query(int param1, int param2, float* direction, int param4,
                            int* results, unsigned int mask, unsigned int param7)
{
    // Algorithm:
    // 1. Compute endpoint: origin + direction
    // 2. Build AABB: min(origin, endpoint) - radius, max(origin, endpoint) + radius
    // 3. Query all nodes overlapping AABB (up to 200)
    // 4. For each hit node: call FUN_80078540 for swept volume test
    // See Ghidra at 0x8005bbcc
    (void)param1; (void)param2; (void)direction; (void)param4;
    (void)results; (void)mask; (void)param7;
}

// =============================================================================
// Address: 0x8005bd68 | scene_per_frame_update
// Scene per-frame update dispatcher.
// Calls room streaming, block streaming, and slot visibility in sequence.
// =============================================================================
void scene_per_frame_update(void)
{
    scene_room_streaming_update(/* passthrough params */);
    scene_streaming_update(/* passthrough params */);
    scene_update_slot_visibility();
}

// =============================================================================
// Address: 0x8005bd90 | scene_portal_compute_area
// Computes approximate area of a portal polygon.
// Triangles: area = edge1_len * edge2_len * 0.5
// Quads+: area = edge1_len * edge2_len
// Used by portal priority selection to favor larger portals.
// =============================================================================
double scene_portal_compute_area(unsigned int* portal)
{
    if (*portal <= 2) {
        return (double)FLOAT_80613dfc;  // Degenerate polygon
    }

    // portal[0] = vertex count
    // portal[0x0F..0x11] = vertex 0 (XYZ at indices 15,16,17)
    // portal[0x09..0x0B] = vertex 1 (XYZ at indices 9,10,11)
    // portal[0x15..0x17] = vertex 2 (XYZ at indices 21,22,23)

    // Edge 1: vertex1 - vertex0
    float dx1 = (float)portal[0x09] - (float)portal[0x0F];
    float dy1 = (float)portal[0x0A] - (float)portal[0x10];
    float dz1 = (float)portal[0x0B] - (float)portal[0x11];
    double edge1_len = FUN_80131538((double)(dx1*dx1 + dy1*dy1 + dz1*dz1));

    // Edge 2: vertex2 - vertex0
    float dx2 = (float)portal[0x15] - (float)portal[0x0F];
    float dy2 = (float)portal[0x16] - (float)portal[0x10];
    float dz2 = (float)portal[0x17] - (float)portal[0x11];
    double edge2_len = FUN_80131538((double)(dx2*dx2 + dy2*dy2 + dz2*dz2));

    double area = edge1_len * edge2_len;
    if (*portal == 3) {
        area *= (double)FLOAT_80613de0;  // * 0.5 for triangle
    }

    return area;
}

// =============================================================================
// Address: 0x8005be8c | scene_portal_get_ptr
// Returns portal data pointer from the portal pointer table.
// Simple lookup: DAT_80611aac[portal_id].
// =============================================================================
void* scene_portal_get_ptr(int portal_id)
{
    return *(void**)(DAT_80611aac + portal_id * 4);
}

// =============================================================================
// Address: 0x8005be9c | scene_compute_plane_from_tri
// Computes a normalized plane equation from 3 points (triangle).
// Output: param_1[0..2] = normalized normal, param_1[3] = plane distance.
// Uses fast inverse sqrt (Newton-Raphson refinement).
// =============================================================================
void scene_compute_plane_from_tri(float* plane_out, float* p0, float* p1, float* p2)
{
    // Edge vectors
    float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
    float e2x = p2[0] - p1[0], e2y = p2[1] - p1[1], e2z = p2[2] - p1[2];

    // Cross product (normal)
    plane_out[0] = e1y * e2z - e1z * e2y;
    plane_out[1] = e1z * e2x - e1x * e2z;
    plane_out[2] = e1x * e2y - e1y * e2x;

    // Normalize with Newton-Raphson fast inverse sqrt
    float len_sq = plane_out[0]*plane_out[0] + plane_out[1]*plane_out[1] + plane_out[2]*plane_out[2];
    if (FLOAT_80613e10 < len_sq) {
        float inv_len = 1.0f / sqrtf(len_sq);
        // Two Newton-Raphson iterations for precision
        inv_len = inv_len * -(FLOAT_80613e14 * len_sq * inv_len * inv_len - FLOAT_80613e18);
        float scale = FLOAT_80613e1c * inv_len * -(FLOAT_80613e14 * len_sq * inv_len * inv_len - FLOAT_80613e18);
        plane_out[0] *= scale;
        plane_out[1] *= scale;
        plane_out[2] *= scale;
    }

    // Plane distance = -dot(normal, point)
    plane_out[3] = -(plane_out[0]*p0[0] + plane_out[1]*p0[1] + plane_out[2]*p0[2]);
}

// =============================================================================
// Address: 0x8005bfa4 | scene_portal_build_clip_planes
// Builds clip planes for a portal polygon relative to the camera.
// Generates N+1 planes: one face plane (scaled by front/back factor)
// and one edge plane per polygon edge (from camera through edge).
// =============================================================================
void scene_portal_build_clip_planes(void)
{
    // Algorithm:
    // 1. Get portal pointer and camera position
    // 2. Determine front/back facing: dot(portal_normal, camera_pos) > 0 => front
    // 3. Scale factor: front = 1.0 (FLOAT_80613e1c), back = -1.0 (FLOAT_80613e20)
    // 4. First plane = portal face normal * scale + portal distance * scale
    // 5. For each edge: compute plane from camera + edge vertices, scale by factor
    // See Ghidra at 0x8005bfa4
}

// =============================================================================
// Address: 0x8005c0dc | scene_portal_test_clip_planes
// Tests N vertices against a portal's clip planes.
// Returns 1 if ALL vertices pass (inside all planes), 0 if any vertex fails.
// Used during PVS traversal to early-reject invisible portals.
// =============================================================================
int scene_portal_test_clip_planes(float* vertices, int vertex_count, int param3, int* clip_data)
{
    int num_planes = *(short*)(*clip_data + 0x10) + 1;

    for (int v = 0; v < vertex_count; v++) {
        float* vert = vertices + v * 3;
        int plane_offset = 0;

        for (int p = 0; p < num_planes; p++) {
            float* plane = (float*)((char*)clip_data + plane_offset + 8);
            float dist = plane[0]*vert[0] + plane[1]*vert[1] + plane[2]*vert[2] + plane[3];
            if (dist < FLOAT_80613e24) {
                return 0;  // Vertex outside this plane
            }
            plane_offset += 0x10;
        }
    }

    return 1;  // All vertices inside all planes
}

// =============================================================================
// Address: 0x8005c168 | scene_portal_priority_insert
// Inserts a portal into the per-viewport priority queue (max 10 entries).
// If queue not full: append. If full: replace entry with smallest area
// if new portal has larger projected area.
// Marks portal flag bit 0 to indicate selection.
// =============================================================================
void scene_portal_priority_insert(double projected_area, int portal_vis_ptr)
{
    int viewport_idx = *(int*)(DAT_80611d54 + 0x214);
    int* count_ptr = (int*)(&DAT_8052d948 + viewport_idx * 4);

    if (*count_ptr < 10) {
        // Queue not full: append
        *(unsigned short*)(portal_vis_ptr + 0x12) |= 1;  // Mark selected
        // Store portal and area at next slot
        // ... (queue management)
        (*count_ptr)++;
    } else {
        // Queue full: find entry with smallest area
        double min_area = /* first entry area */0;
        int min_idx = 0;

        for (int i = 1; i < 10; i++) {
            // Compare areas, track minimum
        }

        if (projected_area > min_area) {
            // Replace: clear old portal's flag, insert new
            // ... (replacement logic)
            *(unsigned short*)(portal_vis_ptr + 0x12) |= 1;
        }
    }
}

// =============================================================================
// Address: 0x8005c2b4 | Name: scene_portal_select_visible | Batch: 149
// Date: 2026-08-10
// Description: Selects the most visible portals for PVS traversal. For each
//   portal in the scene: computes dot product with view direction, checks if
//   any vertices are in front of camera, computes projected screen area
//   (proportional to solid angle), and inserts into priority queue. Portals
//   facing away or fully behind camera are excluded. Max 10 portals selected.
// =============================================================================

void scene_portal_select_visible(void) {
    float view_dir[3];
    float dot_product;
    float centroid[3];
    float area_estimate;
    int portal_idx;
    int portal_stride;
    float* portal_data;
    int vertex_count;
    uint visibility_mask;
    int i;

    FUN_803826ec(); // perf counter begin

    int total_portals = DAT_80611b0c;
    float cam_dir_x = *(float*)(DAT_80611d54 + 0x338);
    float cam_dir_y = *(float*)(DAT_80611d54 + 0x334);
    float cam_dir_z = *(float*)(DAT_80611d54 + 0x33c);

    // Normalize camera direction
    float len_sq = cam_dir_z * cam_dir_z + (cam_dir_y * cam_dir_y + cam_dir_x * cam_dir_x);
    if (EPSILON_SMALL < len_sq) {
        float inv_len = rsqrt_newton(len_sq);
        cam_dir_x *= inv_len;
        cam_dir_y *= inv_len;
        cam_dir_z *= inv_len;
    }

    float min_dot_threshold = FLOAT_80613e28; // minimum solid angle threshold

    // Reset visible portal count for current frame
    *(int*)(&DAT_8052d948 + *(int*)(DAT_80611d54 + 0x214) * 4) = 0;

    portal_idx = 1;
    portal_stride = 4;

    while (portal_idx <= total_portals &&
           *(int*)(&DAT_8052d948 + *(int*)(DAT_80611d54 + 0x214) * 4) < 10) {

        area_estimate = 0.0f;
        portal_data = *(float**)(DAT_80611ab4 + portal_stride);

        // Dot product of portal normal with camera direction
        dot_product = portal_data[2] * cam_dir_z +
                      (portal_data[0] * cam_dir_y + portal_data[1] * cam_dir_x);

        bool should_insert = (min_dot_threshold <= fabsf(dot_product));

        if (should_insert) {
            // Check if any portal vertices are in front of the camera
            visibility_mask = 0;
            centroid[0] = 0.0f;
            centroid[1] = 0.0f;
            centroid[2] = 0.0f;
            vertex_count = (uint)*(short*)(portal_data + 4);

            for (i = 0; i < (int)vertex_count; i++) {
                float vx = *(float*)((int)portal_data + i * 12 + 0x14);
                float vy = *(float*)((int)portal_data + i * 12 + 0x18);
                float vz = *(float*)((int)portal_data + i * 12 + 0x1c);

                centroid[0] += vx;
                centroid[1] += vy;
                centroid[2] += vz;

                // Classify vertex: behind or in front of near plane
                float plane_dist = cam_dir_z * vz + cam_dir_y * vx + cam_dir_x * vy;
                float cam_plane = cam_dir_z * *(float*)(DAT_80611d54 + 0x284) +
                                  cam_dir_y * *(float*)(DAT_80611d54 + 0x27c) +
                                  cam_dir_x * *(float*)(DAT_80611d54 + 0x280);

                if (0.0f <= plane_dist - cam_plane) {
                    visibility_mask |= 2; // vertex in front
                } else {
                    visibility_mask |= 1; // vertex behind
                }
            }

            if (visibility_mask == 1) {
                // All vertices behind camera
                should_insert = false;
            } else {
                // Compute area estimate: solid angle approximation
                float inv_count = 1.0f / (float)vertex_count;
                float cx = centroid[1] * inv_count - *(float*)(DAT_80611d54 + 0x280);
                float cy = centroid[0] * inv_count - *(float*)(DAT_80611d54 + 0x27c);
                float cz = centroid[2] * inv_count - *(float*)(DAT_80611d54 + 0x284);
                float dist_sq = cz * cz + cy * cy + cx * cx;
                area_estimate = fabsf(dot_product) / dist_sq;
            }
        }

        if (should_insert) {
            scene_portal_priority_insert(area_estimate, (int)portal_data);
        } else {
            // Mark portal as not visible this frame
            *(unsigned short*)((int)portal_data + 0x12) &= ~0x0001;
        }

        portal_idx++;
        portal_stride += 4;
    }

    FUN_80382738(); // perf counter end
}

// =============================================================================
// Address: 0x8005c528 | Name: scene_portal_prepass | Batch: 149
// Date: 2026-08-10
// Description: Portal visibility pre-pass. Called before the main PVS traversal.
//   First selects the most important portals (by projected area), then builds
//   clip planes for each selected portal. The clip planes are used during
//   the recursive traversal to tightly cull geometry behind portals.
// =============================================================================

void scene_portal_prepass(void) {
    FUN_803826ec(); // perf counter begin

    scene_portal_select_visible();

    int visible_count = *(int*)(&DAT_8052d948 + *(int*)(DAT_80611d54 + 0x214) * 4);

    for (int i = 0; i < visible_count; i++) {
        scene_portal_build_clip_planes();
    }

    FUN_80382738(); // perf counter end
}

// =============================================================================
// Address: 0x8005c5b0 | Name: scene_post_load_stub | Batch: 149
// Date: 2026-08-10
// Description: Empty stub called from scene_level_load_finalize. Reserved for
//   future use or stripped debug functionality.
// =============================================================================

void scene_post_load_stub(void) {
    return;
}
