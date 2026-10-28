// Batch 344 | Date: 2026-09-04
// HUD objective marker / compass. Two parts:
//
//   0x80125654 hud_compass_direction_tick  (per-frame: compute the bearing +
//                                            pitch toward the current objective
//                                            zone, smoothed into the view state)
//   0x80127180 hud_objective_marker_render  (MASSIVE: draw the 3D objective
//                                            marker entity + story-mode arrow /
//                                            deathmatch radar dots)
//
// The current objective zone id is resolved per game mode (story / mapmaker /
// objective) via the soundpad zone graph and stored in DAT_80611fdc; the smoothed
// bearing/pitch are stored in the view state at +0x1734 / +0x1738.
// Subsystem: game (HUD rendering)
//
// Compile-Clean Standard: valid signatures + faithful bodies; raw symbols
// declared per-file below with call-site types.

#include "common.h"

// ============================================================================
// Per-file externs for raw symbols.
// ============================================================================
extern float  FLOAT_80615718;   // 1.0
extern float  FLOAT_80615730;   // 0.0
extern float  FLOAT_80615734;   // 0.5
extern float  FLOAT_80615748;   // 2.0
extern float  FLOAT_80615750;
extern float  FLOAT_8061576c;
extern float  FLOAT_8061579c;
extern float  FLOAT_806157b4;
extern float  FLOAT_806157c0;
extern float  FLOAT_806157c4;
extern float  FLOAT_806157c8;
extern float  FLOAT_806157e0;
extern float  FLOAT_80615814;
extern float  FLOAT_80615848;
extern float  FLOAT_80615860;
extern float  FLOAT_8061594c;
extern float  FLOAT_806159a4;
extern float  FLOAT_806159b0;
extern float  FLOAT_806159b4;
extern float  FLOAT_806159b8;
extern float  FLOAT_806159bc;
extern float  FLOAT_806159c0;
extern float  FLOAT_806159c4;
extern float  FLOAT_806159c8;
extern float  FLOAT_806159cc;
extern float  FLOAT_806159d0;
extern double DOUBLE_80615738;  // uint->double bias

extern int    DAT_80611d74;     // active HUD view base
extern int    DAT_80611d54;     // active render/camera context
extern int    DAT_80611b8c;     // zone node table base (0x20 stride)
extern int    DAT_80611ca0;     // portal record table base (0x98 stride)
extern int    DAT_80611ca4;     // portal record count
extern int    DAT_80611fdc;     // current objective zone id (-1 = none)
extern int    DAT_80611f78;     // objective marker entity ptr
extern int    DAT_805015c0;     // mode flags (bit 0x40 = objective mode)
extern int    DAT_805015d8;     // objective/challenge id
extern unsigned char DAT_805015bd;   // game phase byte
extern int    DAT_80612610;     // active player count
extern int    DAT_80612614;     // special objective id
extern int    DAT_80611928;     // frame counter
extern int    DAT_806109c0;     // frame delta accumulator (uint magnitude)
extern int    DAT_806108e4;     // frame delta ticks
extern float  DAT_80583228;     // sine LUT (indexed via &NAME)
extern float  DAT_8040d2a0;     // portal-weight table (via &NAME)
extern int    DAT_8057e9a0;     // radar-dot fade state array (via &NAME)

// named helpers (raw)
extern int    FUN_803826dc(void);            // HUD push (compass), returns state block
extern void   FUN_80382728(void);            // HUD pop  (compass)
extern int    FUN_803826cc(void);            // HUD push (marker), returns state block
extern void   FUN_80382718(void);            // HUD pop  (marker)
extern void   FUN_8022d878(float* outPos, float* outYaw);          // player pos/facing
extern int    soundpad_find_nearest_entity(double minDist, float* pos);
extern int    FUN_8022de70(void);            // story-mode objective zone
extern int    FUN_8021a7bc(void);            // mapmaker objective zone (special 0x1a)
extern int    soundpad_find_zone_by_id(int id);
extern int    FUN_8022d634(void);            // objective-mode zone
extern void   soundpad_portal_deactivate(int portal);
extern void   soundpad_portal_activate(int portal);
extern int    FUN_801389b0(int ctx, int* fromZone);   // next zone step toward target
extern double FUN_80390aa4(double y, double x);        // atan2
extern double FUN_80390324(double x);                  // round-to-int helper
extern int    FUN_801c1714(void);                      // split-screen count
extern int*   soundpad_resolve_zone_node(int zoneId);
extern int    FUN_80220f70(int a);
extern uint   FUN_80220f30(int a);
extern uint   FUN_80220f50(int a);
extern double FUN_80131538(double x);                  // sqrt
extern int    game_get_round_timer(void);
extern void   entity_apply_vertex_color_offset_full(int ent, int slot, char a, char b, char c,
                                                    char d, int e, int f, int g, unsigned char alpha);
extern void   FUN_80171f9c(float* p);
extern void   FUN_8016e84c(void);
extern void   FUN_8016e8ac(int blend);
extern void   FUN_8016f00c(int mode);
extern unsigned long long entity_force_bone_update(int ent);
extern unsigned long long render_instance_draw_pass(unsigned long long st, double p2, int inst, int pass, int a);
extern void   spawn_hud_marker(unsigned long long p1, double p2, double p3, unsigned long long p4,
                               unsigned long long p5, unsigned long long p6, unsigned long long p7,
                               unsigned long long p8);   // batch 343
extern int    sprite_get_texture_id(int spriteId);
extern int*   FUN_8022da98(void);            // radar-dot record (story)
extern int    FUN_8022db38(int a, float* posOut, int* entOut, int max);
extern void   scene_find_nodes_containing_point(float* pos, int* outRoom, int flag);
extern int    scene_raycast(int room, float* origin, float* dir, int a, float* b, float* c,
                            int* d, int mask, int e, int* f);
extern int    hud_target_box_render(double p1, double p2, float* pos, int ent, int sprite,
                                    uint color, uint flag);   // FUN_801259ac (hud_target_box.cpp)

// forward decls (this file)
extern "C" {
void hud_compass_direction_tick(void);
void hud_objective_marker_render(unsigned long long p1, double p2, double p3,
                                 unsigned long long p4, unsigned long long p5,
                                 unsigned long long p6, unsigned long long p7,
                                 unsigned long long p8);
}

// ============================================================================
// 0x80125654 | hud_compass_direction_tick
// Per-frame: resolves the current objective zone (per game mode) into
// DAT_80611fdc, walks up to 5 zone-graph steps from the player toward it
// (FUN_801389b0) accumulating a weighted target position, computes the bearing
// (atan2) and pitch, and eases the view-state bearing/pitch (+0x1734/+0x1738)
// toward the new values. Temporarily deactivates/reactivates blocked portals so
// the path search can cross them.
// ============================================================================
extern "C" void hud_compass_direction_tick(void) {
    int blockedPortals[32];
    float px, py, pz, facing;   // local_b4, local_b0, local_ac, local_b8

    int state = FUN_803826dc();
    int nBlocked = 0;
    int blockedIdx = 0;
    int ctx = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
    int savedZone = *(int*)(ctx + 0x678);

    // player position: from the objective-mode player, else from the view
    if (((DAT_805015c0 & 0x40) == 0) ||
        (((DAT_805015d8 != 6) && (DAT_805015d8 != 7)) && (DAT_805015d8 != 8))) {
        px = *(float*)(DAT_80611d74 + 0xa8);
        py = *(float*)(DAT_80611d74 + 0xac);
        pz = *(float*)(DAT_80611d74 + 0xb0);
        facing = *(float*)(DAT_80611d74 + 0x100);
    } else {
        FUN_8022d878(&px, &facing);
        *(int*)(ctx + 0x678) = soundpad_find_nearest_entity((double)FLOAT_80615730, &px);
    }

    // resolve the objective zone id per mode
    if (DAT_805015bd == '\x03') {
        DAT_80611fdc = FUN_8022de70();
    } else if ((DAT_805015c0 & 0x40) == 0) {
        if (DAT_805015bd == '\n') {
            if (DAT_80612614 == 0x1a) {
                DAT_80611fdc = FUN_8021a7bc();
            } else if ((DAT_80612614 < 0x1a) && (DAT_80612614 == 0x11)) {
                DAT_80611fdc = soundpad_find_zone_by_id(0x400);
            }
        }
    } else {
        DAT_80611fdc = FUN_8022d634();
    }

    if (DAT_80611fdc != -1) {
        // temporarily deactivate blocked portals so pathing can cross them
        int recOff = 0;
        for (int i = 0; i < DAT_80611ca4; i++) {
            int rec = DAT_80611ca0 + recOff;
            if ((*(uint*)(rec + 0xc) & 2) != 0) {
                blockedPortals[blockedIdx] = rec;
                blockedIdx += 1;
                nBlocked++;
                soundpad_portal_deactivate(*(int*)(rec + 0x44));
            }
            recOff += 0x98;
        }

        double tx = (double)px;
        double tz = (double)pz;
        double weightSum = (double)FLOAT_80615718;
        *(int*)(ctx + 0x9a8) = 0;
        *(int*)(ctx + 0x9ac) = 0;
        *(int*)(ctx + 0x684) = DAT_80611fdc;
        int step = 0;
        int weightOff = 0;
        do {
            int nextZone = FUN_801389b0(*(int*)(DAT_80611d74 + 0x14), (int*)(ctx + 0x678));
            if (nextZone < 0) {
                int node = DAT_80611b8c + DAT_80611fdc * 0x20;
                weightSum = (double)FLOAT_80615748;
                tx = (double)(float)(tx + (double)*(float*)(node + 0xc));
                tz = (double)(float)(tz + (double)*(float*)(node + 0x14));
                break;
            }
            double w = (double)*(float*)((int)&DAT_8040d2a0 + weightOff);
            int node = DAT_80611b8c + nextZone * 0x20;
            weightSum = (double)(float)(weightSum + w);
            tx = (double)(float)(tx + (double)(float)((double)*(float*)(node + 0xc) * w));
            tz = (double)(float)(tz + (double)(float)((double)*(float*)(node + 0x14) * w));
            if (nextZone == DAT_80611fdc) break;
            *(int*)(ctx + 0x678) = nextZone;
            step++;
            weightOff += 4;
            *(int*)(ctx + 0x9a8) = 0;
            *(int*)(ctx + 0x9ac) = 0;
        } while (step < 5);

        // reactivate the portals we deactivated
        int bOff = 0;
        for (int i = 0; i < nBlocked; i++) {
            soundpad_portal_activate(*(int*)(blockedPortals[i] + 0x44));
            bOff += 4;
        }

        double one = (double)FLOAT_80615718;
        *(int*)(ctx + 0x678) = savedZone;
        double bearing = (double)FUN_80390aa4((double)(pz - (float)(tz * (double)(float)(one / weightSum))),
                                              (double)(px - (float)(tx * (double)(float)(one / weightSum))));
        // convert bearing to the HUD angle domain, relative to the player facing
        double angle = (double)((FLOAT_806159a4 * (float)bearing) / FLOAT_806157c4 + FLOAT_8061594c + facing);
        // ease bearing (+0x1734) then pitch (+0x1738) toward the new angle
        double r1 = FUN_80390324((double)((float)(angle - (double)FLOAT_806159a4) / FLOAT_806157e0));
        *(float*)(state + 0x1734) = -(float)((double)FLOAT_806157e0 * (double)(float)r1 - angle);
        double d = (double)(*(float*)(state + 0x1734) - *(float*)(state + 0x1738));
        double r2 = FUN_80390324((double)(float)((double)(float)(d - (double)FLOAT_806159a4) / (double)FLOAT_806157e0));
        *(float*)(state + 0x1738) =
            -(float)((double)FLOAT_806157e0 * (double)(float)r2 - d) * FLOAT_80615814 + *(float*)(state + 0x1738);
    }
    FUN_80382728();
    return;
}

// ============================================================================
// 0x80127180 | hud_objective_marker_render  (MASSIVE)
// Draws the on-screen objective marker. Spawns the marker entity lazily
// (spawn_hud_marker) if needed, positions it in screen space (scaled per split
// layout), fades/pulses it by distance in story mode, applies the per-slot vertex
// color, then renders it in 3 passes. In story mode it also blends the compass
// dot; in deathmatch it iterates the radar dots (FUN_8022db38) drawing each via
// hud_target_box_render with an occlusion raycast.
// ============================================================================
extern "C" void hud_objective_marker_render(unsigned long long p1, double p2, double p3,
                                            unsigned long long p4, unsigned long long p5,
                                            unsigned long long p6, unsigned long long p7,
                                            unsigned long long p8) {
    float dotPos[93];    // local_13c (31*3)
    int   dotEnt[10];    // local_164
    int   room;          // local_180
    float rayDir[3];     // local_170..168
    float pt[3];         // local_17c..174

    int state = FUN_803826cc();
    if (DAT_80611f78 == 0) {
        spawn_hud_marker(p1, p2, p3, p4, p5, p6, p7, p8);
    }
    if (DAT_80611f78 != 0) {
        bool dim = false;
        int alpha = 0x40;
        double mx, my, mz;   // dVar20/21/22 marker screen offset by split layout
        int split = FUN_801c1714();
        if (split == 1) {
            mx = (double)FLOAT_80615730; my = (double)FLOAT_806159b0; mz = (double)FLOAT_806159b4;
        } else if ((split == 2) ||
                   ((FUN_801c1714() == 3) && (*(int*)(DAT_80611d74 + 4) == 0))) {
            mx = (double)FLOAT_80615730; my = (double)FLOAT_806159b8; mz = (double)FLOAT_806159bc;
        } else {
            mx = (double)FLOAT_80615730; my = (double)FLOAT_806159c0; mz = (double)FLOAT_806159c4;
        }

        if (DAT_805015bd == '\n') {
            // distance to objective zone -> fade/pulse
            int* node = soundpad_resolve_zone_node(DAT_80611fdc);
            double dy = (double)(*(float*)(DAT_80611d74 + 0xac) - (float)node[4]);
            double dsq = (double)(float)((double)(*(float*)(DAT_80611d74 + 0xac) - (float)node[4]) * dy);
            node = soundpad_resolve_zone_node(DAT_80611fdc);
            double dx = (double)(*(float*)(DAT_80611d74 + 0xa8) - (float)node[3]);
            dsq = (double)(float)((double)(*(float*)(DAT_80611d74 + 0xa8) - (float)node[3]) * dx + dsq);
            node = soundpad_resolve_zone_node(DAT_80611fdc);
            double dz = (double)(*(float*)(DAT_80611d74 + 0xb0) - (float)node[5]);
            double dist = (double)(float)((double)(*(float*)(DAT_80611d74 + 0xb0) - (float)node[5]) * dz + dsq);

            if (DAT_80612614 == 0x11) {
                if (FUN_80220f70(3) == 0) dim = true;
            } else if ((DAT_80612614 == 0x1a) &&
                       ((FUN_80220f70(0) == 0) ||
                        ((FUN_80220f70(1) != 0) && (FUN_80220f30(1) == 0) && (FUN_80220f50(1) == 0)))) {
                dim = true;
            }

            if ((double)FLOAT_806159c8 <= dist) {
                if (dist < (double)FLOAT_806157c8) {
                    double d = FUN_80131538(dist);
                    uint pi = (uint)(FLOAT_806157c0 *
                        (float)((double)FLOAT_806157c4 *
                                (double)(float)((double)(float)(d - (double)FLOAT_8061579c) / (double)FLOAT_8061579c) -
                                (double)FLOAT_80615860) + FLOAT_80615734);
                    int a = (int)(FLOAT_806157b4 * (FLOAT_80615734 * (&DAT_80583228)[pi & 0x7ff] + FLOAT_80615734));
                    alpha = (a < 0x41) ? a : 0x40;
                    if (alpha < 1) alpha = 1;
                }
            } else {
                dim = true;
            }
        }

        unsigned char av = (unsigned char)alpha;
        if (((DAT_805015bd == '\n') && (1 < DAT_80612610)) &&
            ((game_get_round_timer() != 0) && (*(int*)(DAT_80611d74 + 4) == 1))) {
            dim = true;
        }
        if (dim) {
            av = 1;
            my = (double)(float)(my - (double)FLOAT_806159cc);
        }

        *(int*)(*(int*)(DAT_80611f78 + 4) + 0x1b0) = 0;
        for (int slot = 1; slot <= 6; slot++) {
            entity_apply_vertex_color_offset_full(DAT_80611f78, slot, 0, 0, 0, 0, 0, 0, 0, av);
        }
        if (*(float**)(DAT_80611d54 + 0x204) != (float*)0) {
            FUN_80171f9c(*(float**)(DAT_80611d54 + 0x204));
        }
        FUN_8016e84c();
        *(float*)(DAT_80611f78 + 0x70) = FLOAT_80615848 - *(float*)(state + 0x1738);
        *(unsigned char*)(DAT_80611f78 + 0x32) = 1;
        *(unsigned char*)(DAT_80611f78 + 0x31) = 1;
        *(float*)(DAT_80611f78 + 0x64) = (float)mx;
        *(float*)(DAT_80611f78 + 0x68) = (float)my;
        *(float*)(DAT_80611f78 + 0x6c) = (float)mz;
        *(uint*)(DAT_80611f78 + 0x28) = *(uint*)(DAT_80611f78 + 0x28) | 0x1000;
        *(uint*)(DAT_80611f78 + 0x10) = *(uint*)(DAT_80611f78 + 0x10) | 1;
        *(unsigned short*)(DAT_80611f78 + 0x14) =
            *(unsigned short*)(DAT_80611f78 + 0x14) & ~(unsigned short)(1 << *(int*)(DAT_80611d54 + 0x214));
        unsigned long long boned = entity_force_bone_update(DAT_80611f78);
        FUN_8016e8ac(0);
        FUN_8016f00c(1);
        boned = render_instance_draw_pass(boned, p2, *(int*)(DAT_80611f78 + 4), 0, 0);
        boned = render_instance_draw_pass(boned, p2, *(int*)(DAT_80611f78 + 4), 1, 0);
        render_instance_draw_pass(boned, p2, *(int*)(DAT_80611f78 + 4), 2, 0);
        FUN_8016e84c();
    }

    // deathmatch radar dots (story mode uses the single record path)
    if (DAT_805015bd == '\x03') {
        int* rec = FUN_8022da98();
        int dotTex = sprite_get_texture_id(0x127);
        if (*(short*)((int)rec + 4) != *(short*)((int)rec + 10)) {
            for (int i = 0; i < 10; i++) (&DAT_8057e9a0)[i] = 0;
        }
        FUN_8016f00c(2);
        if (rec[0x26] == 0) {
            int n = FUN_8022db38(0, dotPos, dotEnt, 10);
            double half = (double)FLOAT_80615734;
            double one = (double)FLOAT_80615718;
            int posOff = 0;
            int idx = 0;
            double bias = DOUBLE_80615738;
            for (int i = 0; i < n; i++) {
                int ent = dotEnt[idx / 4];
                float* fade = (float*)((int)&DAT_8057e9a0 + idx);
                if ((ent != 0) && ((*(uint*)(ent + 0x28) & 0x800000) != 0)) {
                    *fade = FLOAT_80615730;
                }
                uint fv = (uint)*fade;
                double frac = -(double)(float)((double)(*fade - (float)((double)CONCAT44(0x43300000, fv ^ 0x80000000) - bias)) * half - one);
                if ((fv & 1 ^ fv >> 0x1f) != fv >> 0x1f) {
                    frac = (double)(float)((double)FLOAT_80615748 - frac);
                }
                *fade = (float)(one / (double)(float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - bias)) *
                        (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - bias) + *fade;

                if ((ent == 0) || ((*(uint*)(ent + 0x28) & 0x800000) == 0)) {
                    float* pos = (float*)((int)dotPos + posOff);
                    scene_find_nodes_containing_point(pos, &room, 1);
                    rayDir[0] = *(float*)(DAT_80611d54 + 0x27c) - pos[0];
                    rayDir[1] = *(float*)(DAT_80611d54 + 0x280) - pos[1];
                    rayDir[2] = *(float*)(DAT_80611d54 + 0x284) - pos[2];
                    int hit = scene_raycast(room, pos, rayDir, 0, 0, 0, 0, 0xfffffff9, 0, 0);
                    if ((hit != 0) && (ent != 0)) {
                        int side = 0;
                        double ex = (double)(*(float*)(ent + 100) + *(float*)(ent + 0x1a8));
                        double ey = (double)(*(float*)(ent + 0x68) + *(float*)(ent + 0x1ac));
                        double ez = (double)(*(float*)(ent + 0x6c) + *(float*)(ent + 0x1b0));
                        do {
                            pt[0] = (float)ex; pt[1] = (float)ey; pt[2] = (float)ez;
                            if (side == 0) pt[0] = (float)(ex + (double)*(float*)(ent + 0x1c0));
                            else if (side == 1) pt[0] = (float)(ex - (double)*(float*)(ent + 0x1c0));
                            else if (side == 2) pt[2] = (float)(ez + (double)*(float*)(ent + 0x1c8));
                            else if (side == 3) pt[2] = (float)(ez - (double)*(float*)(ent + 0x1c8));
                            rayDir[0] = *(float*)(DAT_80611d54 + 0x27c) - pt[0];
                            rayDir[1] = *(float*)(DAT_80611d54 + 0x280) - pt[1];
                            rayDir[2] = *(float*)(DAT_80611d54 + 0x284) - pt[2];
                            hit = scene_raycast(*(int*)(ent + 0x34), pt, rayDir, 0, 0, 0, 0, 0xfffffff9, 0, 0);
                        } while ((hit != 0) && (side = side + 1, side < 4));
                    }
                    if ((ent != 0) && ((*(short*)(ent + 0x94) == (short)0x8f) &&
                                       (*(float*)(DAT_80611d54 + 0x27c) < FLOAT_806159d0))) {
                        hit = 1;
                    }
                    if (hit == 0) {
                        int shown = hud_target_box_render((double)FLOAT_80615730, frac, pos, ent, dotTex, 0x3e18642f,
                                                          (uint)(*fade < FLOAT_80615718));
                        if (shown == 0) *fade = FLOAT_80615730;
                    } else {
                        *fade = FLOAT_80615730;
                    }
                }
                posOff += 0xc;
                idx += 4;
            }
        } else {
            uint fv = (uint)(&DAT_8057e9a0)[0];
            double bias = DOUBLE_80615738;
            double frac = -(double)(((float)(&DAT_8057e9a0)[0] - (float)((double)CONCAT44(0x43300000, fv ^ 0x80000000) - bias)) * FLOAT_80615734 - FLOAT_80615718);
            if ((fv & 1 ^ fv >> 0x1f) != fv >> 0x1f) {
                frac = (double)(float)((double)FLOAT_80615748 - frac);
            }
            (&DAT_8057e9a0)[0] = (int)((FLOAT_80615718 / (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - bias)) *
                (float)((double)CONCAT44(0x43300000, DAT_806108e4 ^ 0x80000000) - bias) + (float)(&DAT_8057e9a0)[0]);
            int shown = hud_target_box_render((double)FLOAT_80615730, frac, (float*)(rec + 0x27), 0, dotTex, 0x3e18642f,
                                              (uint)((float)(&DAT_8057e9a0)[0] < FLOAT_80615718));
            if (shown == 0) (&DAT_8057e9a0)[0] = 0;
        }
    }
    FUN_80382718();
    return;
}
