// player_move_control.cpp
// Player locomotion + analog-input processing for the local player object
// (global player context DAT_80611d74). These are driven each frame from
// player_main_tick (0x800a9138):
//   - the control-mapping layout builder (player_build_control_map / *_reset),
//     which fills a 0x58-byte "control map" record (input->action bindings) from
//     a control-scheme id and then applies a left/right-handed swap fixup;
//   - the movement/turn animation selector (player_select_move_anim), which picks
//     the correct walk/run/strafe/turn animation clip from the desired move+turn
//     rates and the player state flags;
//   - the analog-stick readers (player_read_move_input / player_scale_move_input)
//     that convert raw pad axes into clamped, dead-zoned move + aim vectors; and
//   - player_apply_aim_delta, which integrates the aim vector into the player
//     look angles with per-axis wrap/clamp.
//
// Address range: 0x80254cc4 - 0x80255e10  (batch_731)
//
// Self-contained translation unit: all raw ROM symbols and helper prototypes are
// declared file-locally with extern "C" linkage matching the ROM symbol names.
// The player context DAT_80611d74 is the raw base address (int); all field
// accesses are expressed as explicit byte offsets to keep one consistent type.

typedef unsigned char      undefined1;
typedef unsigned short     undefined2;
typedef unsigned int       undefined4;
typedef unsigned int       uint;
typedef unsigned short     ushort;
typedef long long          longlong;
typedef unsigned long long undefined8;

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}
static inline double ABS_D(double x) { return x < 0.0 ? -x : x; }
static inline float  ABS_F(float x)  { return x < 0.0f ? -x : x; }

extern "C" {
// ---- math / anim / input helpers ----
double math_sqrt(double x);
double math_atan2(double y, double x);
double FUN_8038fecc(double x);                         // atan-ish angle helper
double angle_wrap_clamp(double v, double lo, double hi);
double game_get_difficulty_speed(void);
double player_get_camera_lerp_factor(int cam);
double FUN_800a4498(int *player, int axisId);          // read pad axis value
void   input_set_analog_value(double v, int dev);
void   anim_set_playback_rate(double rate, int *clip, int flag);
void   anim_play_scaled(double a1, double a2);
int    anim_is_clip_active(int rig, int clipId);
int    anim_primary_clip_userflags(int rig, int clipId);
int    anim_clip_flags_active_slot(int rig, int clipRec, int a3, int mask);
void   anim_set_clip_speed_fields(double v, int *clip);
void   anim_clip_endpoint_pose(int rig, float *outA, float *outB, float *outC, int which);
void   FUN_8038273c(void);                             // cutscene/anim context leave

// ---- raw globals ----
extern int  DAT_80611d74;   // local player context base address
extern int  DAT_80611d54;   // active camera object (0 if none)
extern int  DAT_80611934;   // movement mode selector (8 = difficulty-scaled)
extern uint DAT_806109c0;   // frame counter
extern int  DAT_80610ba0;   // weapon/anim descriptor table base
extern int  DAT_806129b0, DAT_80610fe8, DAT_80610fec;   // input accumulators (reset)

extern float FLOAT_806108ec;   // frame delta scale
extern float FLOAT_80611d80, FLOAT_80611d84; // aim sensitivity (yaw / pitch)
extern float FLOAT_8061192c;   // difficulty speed base

// float / double literal pool (0x80618e30 .. 0x80618eb0)
extern float DAT_80618e30, DAT_80618e38, DAT_80618e40, DAT_80618e48, DAT_80618e50;
extern float FLOAT_80618e58, FLOAT_80618e5c, FLOAT_80618e60, FLOAT_80618e64, FLOAT_80618e68;
extern float FLOAT_80618e6c, FLOAT_80618e70, FLOAT_80618e74, FLOAT_80618e78, FLOAT_80618e7c;
extern float FLOAT_80618e80, FLOAT_80618e84, FLOAT_80618e88, FLOAT_80618e98, FLOAT_80618e9c;
extern float FLOAT_80618ea0, FLOAT_80618ea4, FLOAT_80618ea8, FLOAT_80618eac;
extern double DOUBLE_80618e90;
extern float DAT_80583228;     // sine/cosine lookup table (stride-4 floats)

// ---- intra-file forward decls ----
void FUN_80254cdc(int scheme, int *map);
void FUN_80254e78(int map);

// ============================================================================
// 0x80254cc4  player_move_input_reset
// Clears the per-frame movement input accumulators.
// ============================================================================
void FUN_80254cc4(void)
{
    DAT_806129b0 = 0;
    DAT_80610fe8 = 0;
    DAT_80610fec = 0;
}

// ============================================================================
// 0x80254cd8  player_control_noop
// Empty control hook (registered slot with no behaviour).
// ============================================================================
void FUN_80254cd8(void)
{
}

// ============================================================================
// 0x80254cdc  player_fill_control_map
// Fills the input->action binding fields of a 0x58-byte control-map record for
// the given control scheme. Schemes 1/3 vs 2/4 differ only in which axis is
// mapped to strafe vs turn (fields [0xe]/[0x10] and [0xf]/[0x11] swap).
// ============================================================================
void FUN_80254cdc(int scheme, int *map)
{
    map[6]  = 4;
    map[7]  = 8;
    map[0xb] = 4;
    map[8]  = 1;
    map[9]  = 2;
    if (scheme == 2) {
        map[0]   = 0x400; map[1] = 0x800; map[3] = 0x40; map[5] = 0x10;
        map[0xc] = 8;     map[0xd] = 0x20; map[4] = 0x200; map[2] = 0x80;
        map[0xe] = 4;     map[0x10] = 2;  map[0xf] = 3;   map[0x11] = 1;
        return;
    }
    if (1 < scheme) {
        if (scheme == 4) {
            map[0]   = 0x400; map[1] = 0x800; map[3] = 0x10; map[5] = 0x20;
            map[0xc] = 8;     map[0xd] = 0x40; map[4] = 0x200; map[2] = 0x80;
            map[0xe] = 2;     map[0x10] = 4;  map[0xf] = 3;   map[0x11] = 1;
            return;
        }
        if (3 < scheme) {
            return;
        }
        /* scheme == 3 */
        map[0]   = 0x400; map[1] = 0x800; map[3] = 0x40; map[5] = 0x10;
        map[0xc] = 8;     map[0xd] = 0x20; map[4] = 0x200; map[2] = 0x80;
        map[0xe] = 4;     map[0x10] = 2;  map[0xf] = 1;   map[0x11] = 3;
        return;
    }
    if (scheme == 0) {
        return;
    }
    if (scheme < 0) {
        return;
    }
    /* scheme == 1 */
    map[0]   = 0x400; map[1] = 0x800; map[3] = 0x40; map[5] = 0x10;
    map[0xc] = 8;     map[0xd] = 0x20; map[4] = 0x200; map[2] = 0x80;
    map[0xe] = 2;     map[0x10] = 4;  map[0xf] = 1;   map[0x11] = 3;
}

// ============================================================================
// 0x80254e78  player_fixup_control_map
// Left/right-hand fixup applied after the base map is filled: copies the four
// direction bindings into the "active" slots (+0x48..+0x54) and, for the two
// mirrored schemes (turn==3/strafe==2 or turn==1/strafe==4), swaps the strafe
// and turn action assignments so both handedness variants behave symmetrically.
// ============================================================================
void FUN_80254e78(int map)
{
    *(int *)(map + 0x48) = *(int *)(map + 0x38);
    *(int *)(map + 0x4c) = *(int *)(map + 0x3c);
    *(int *)(map + 0x50) = *(int *)(map + 0x40);
    *(int *)(map + 0x54) = *(int *)(map + 0x44);
    if ((*(int *)(map + 0x54) != 3) || (*(int *)(map + 0x50) != 2)) {
        if (*(int *)(map + 0x54) != 1) {
            return;
        }
        if (*(int *)(map + 0x50) != 4) {
            return;
        }
    }
    *(int *)(map + 0x50) = *(int *)(map + 0x38);
    *(int *)(map + 0x48) = *(int *)(map + 0x40);
}

// ============================================================================
// 0x80254ed8  player_build_control_map
// Builds a control-map record for a scheme id: zero it, fill the bindings, then
// apply the handedness fixup. Called from player_apply_model_def /
// hud_init_weapon_display / FUN_802f5140.
// ============================================================================
void FUN_80254ed8(int scheme, int *map)
{
    /* memset(map, 0, 0x58) */
    for (int i = 0; i < 0x16; i = i + 1) {
        map[i] = 0;
    }
    FUN_80254cdc(scheme, map);
    FUN_80254e78((int)map);
}

// ============================================================================
// 0x80254f30  player_select_move_anim
// Selects and plays the correct locomotion animation clip for the local player
// from the desired forward/strafe rates (param_1 = fwd, param_2 = strafe) and
// the player state flags at DAT_80611d74. Handles: turret/mounted mode
// (flag 0x400), crouch/special (0x20000), idle-vs-move threshold, forward/back
// and strafe left/right run vs walk clip selection (with a crouch 0x10 variant),
// blend of two clips by the dominant axis, facing-angle integration with wrap
// into [-180,180] and snap when the residual exceeds FLOAT_80618e88, and finally
// sets the clip playback speed proportional to the requested move speed.
// ============================================================================
void FUN_80254f30(double param_fwd, double param_strafe)
{
    float  fVar1;
    int    iVar2;
    int    iVar3;
    int    iVar4;
    int    rig;
    double dVar6;
    double dVar7;
    double dVar8;
    double dVar9;
    float  local_60[2];
    float  local_64;
    float  local_68;
    undefined4 local_58;
    uint   uStack_54;

    double dVar_fwd = param_fwd;
    iVar4 = -1;
    rig   = *(int *)(DAT_80611d74 + 0x14);
    iVar3 = *(int *)(rig + 0x7c);

    if ((*(uint *)(DAT_80611d74 + 0xc) & 0x400) == 0) {
        if ((*(uint *)(DAT_80611d74 + 0xc) & 0x20000) == 0) {
            if ((*(uint *)(DAT_80611d74 + 0x114) & 0x31000) == 0) {
                if ((*(uint *)(DAT_80611d74 + 0x114) & 0xb3180) != 0) {
                    dVar7 = (double)FLOAT_80618e58;
                    *(int *)(iVar3 + 0x94) = 0;
                    anim_set_playback_rate(dVar7, *(int **)(rig + 4), 1);
                }
                iVar2 = DAT_80611d74 + *(int *)(DAT_80611d74 + 0x11c) * 4;
                dVar9 = (double)(float)(dVar_fwd * (double)*(float *)(iVar2 + 0x70));
                dVar7 = (double)(float)(param_strafe * (double)*(float *)(iVar2 + 0x78));
                iVar2 = anim_clip_flags_active_slot(rig, iVar2, 0, 0x40000);
                if (iVar2 == 0) {
                    dVar7 = math_sqrt((double)(float)(dVar9 * dVar9 + (double)(float)(dVar7 * dVar7)));
                } else {
                    dVar7 = (double)FLOAT_80618e58;
                }
                if ((double)FLOAT_80618e68 <= dVar7) {
                    if (ABS_D(dVar_fwd) <= ABS_D(param_strafe)) {
                        dVar9 = (double)FLOAT_80618e58;
                        if (dVar9 != dVar_fwd) {
                            dVar8 = FUN_8038fecc((double)(float)(-param_strafe / dVar_fwd));
                            dVar9 = (double)FLOAT_80618e58;
                            fVar1 = (FLOAT_80618e70 * (float)dVar8) / FLOAT_80618e74;
                            if (dVar9 <= dVar_fwd) {
                                if (param_strafe <= dVar9) {
                                    dVar9 = (double)(fVar1 - FLOAT_80618e7c);
                                } else {
                                    dVar9 = (double)(fVar1 + FLOAT_80618e7c);
                                }
                            } else if (param_strafe <= dVar9) {
                                dVar9 = (double)(fVar1 + FLOAT_80618e7c);
                            } else {
                                dVar9 = (double)(fVar1 - FLOAT_80618e7c);
                            }
                        }
                        iVar4 = 0;
                        dVar6 = (double)FLOAT_80618e58;
                        *(float *)(rig + 0xc4) = (float)((double)*(float *)(DAT_80611d74 + 0x100) + dVar9);
                        dVar9 = (double)(float)((double)*(float *)(DAT_80611d74 + 0x100) + dVar9);
                        if (param_strafe <= dVar6) {
                            if ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) == 0) {
                                if (dVar7 <= (double)*(float *)(&DAT_80618e38 + *(int *)(DAT_80611d74 + 0x11c) * 4)) {
                                    if (anim_is_clip_active(rig, 0x2c) == 0) {
                                        anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                    }
                                } else {
                                    if (anim_is_clip_active(rig, 0x28) == 0) {
                                        anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                    }
                                }
                            } else {
                                if (anim_is_clip_active(rig, 0x31) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            }
                        } else if ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) == 0) {
                            if (dVar7 <= (double)*(float *)(&DAT_80618e38 + *(int *)(DAT_80611d74 + 0x11c) * 4)) {
                                if (anim_is_clip_active(rig, 0x2d) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            } else {
                                if (anim_is_clip_active(rig, 0x29) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            }
                        } else {
                            if (anim_is_clip_active(rig, 0x32) == 0) {
                                anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                            }
                        }
                    } else {
                        iVar4 = 2;
                        dVar9 = FUN_8038fecc((double)(float)(-param_strafe / dVar_fwd));
                        dVar9 = (double)(*(float *)(DAT_80611d74 + 0x100) +
                                         (FLOAT_80618e70 * (float)dVar9) / FLOAT_80618e74);
                        if (dVar_fwd <= (double)FLOAT_80618e58) {
                            if ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) == 0) {
                                if (dVar7 <= (double)*(float *)(&DAT_80618e30 + *(int *)(DAT_80611d74 + 0x11c) * 4)) {
                                    if (anim_is_clip_active(rig, 0x2b) == 0) {
                                        anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                    }
                                } else {
                                    if (anim_is_clip_active(rig, 0x27) == 0) {
                                        anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                    }
                                }
                            } else {
                                if (anim_is_clip_active(rig, 0x30) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            }
                        } else if ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) == 0) {
                            if (dVar7 <= (double)*(float *)(&DAT_80618e30 + *(int *)(DAT_80611d74 + 0x11c) * 4)) {
                                if (anim_is_clip_active(rig, 0x2a) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            } else {
                                if (anim_is_clip_active(rig, 0x26) == 0) {
                                    anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                                }
                            }
                        } else {
                            if (anim_is_clip_active(rig, 0x2f) == 0) {
                                anim_play_scaled((double)FLOAT_80618e78, (double)FLOAT_80618e64);
                            }
                        }
                    }
                } else {
                    if ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) == 0) {
                        if (anim_is_clip_active(rig, 0x25) == 0) {
                            anim_play_scaled((double)FLOAT_80618e6c, (double)FLOAT_80618e64);
                        }
                    } else {
                        if (anim_is_clip_active(rig, 0x2e) == 0) {
                            anim_play_scaled((double)FLOAT_80618e6c, (double)FLOAT_80618e64);
                        }
                    }
                    dVar9 = (double)*(float *)(DAT_80611d74 + 0x100);
                }
                *(float *)(iVar3 + 0xb28) = (float)dVar7;
                for (fVar1 = (float)(dVar9 - (double)*(float *)(DAT_80611d74 + 0xea4));
                     fVar1 <= FLOAT_80618e84; fVar1 = fVar1 + FLOAT_80618e80) {
                }
                for (; FLOAT_80618e70 < fVar1; fVar1 = fVar1 - FLOAT_80618e80) {
                }
                if (FLOAT_80618e88 <= ABS_F(fVar1)) {
                    if (fVar1 <= FLOAT_80618e58) {
                        *(float *)(rig + 0xc4) = *(float *)(DAT_80611d74 + 0xea4) - FLOAT_80618e88;
                    } else {
                        *(float *)(rig + 0xc4) = FLOAT_80618e88 + *(float *)(DAT_80611d74 + 0xea4);
                    }
                } else {
                    *(float *)(rig + 0xc4) = (float)dVar9;
                }
                *(int *)(DAT_80611d74 + 0xea4) = *(int *)(rig + 0xc4);
                if (iVar4 < 0) {
                    anim_set_clip_speed_fields((double)FLOAT_80618e64, *(int **)(rig + 4));
                } else {
                    anim_clip_endpoint_pose(rig, local_60, &local_64, &local_68, iVar4);
                    if ((FLOAT_80618e58 == ABS_F(local_64 - local_60[0])) ||
                        (FLOAT_80618e58 != (float)(*(int **)(rig + 4))[100])) {
                        anim_set_clip_speed_fields((double)FLOAT_80618e64, *(int **)(rig + 4));
                    } else {
                        uStack_54 = DAT_806109c0 ^ 0x80000000;
                        local_58 = 0x43300000;
                        anim_set_clip_speed_fields(
                            (double)((float)(dVar7 * (double)local_68) /
                                     (ABS_F(local_64 - local_60[0]) *
                                      (float)((double)CONCAT44(0x43300000, uStack_54) - DOUBLE_80618e90))),
                            *(int **)(rig + 4));
                    }
                }
            } else {
                iVar4 = anim_is_clip_active(rig, 0x25);
                if (iVar4 == 0) {
                    anim_play_scaled((double)FLOAT_80618e60, (double)FLOAT_80618e64);
                }
                dVar6 = (double)FLOAT_80618e58;
                *(int *)(iVar3 + 0x94) = 0;
                anim_set_playback_rate(dVar6, *(int **)(rig + 4), 1);
            }
        } else {
            if (((*(uint *)(DAT_80610ba0 + *(int *)(iVar3 + 0x204) * 0x29c + 0xc) & 1) == 0) ||
                (iVar4 = anim_is_clip_active(rig, 0x34), iVar4 != 0)) {
                if (((*(uint *)(DAT_80610ba0 + *(int *)(iVar3 + 0x204) * 0x29c + 0xc) & 2) != 0) &&
                    (anim_is_clip_active(rig, 0x35) == 0)) {
                    anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
                }
            } else {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
            }
            if ((anim_primary_clip_userflags(rig, 0x34) != 0) ||
                (anim_primary_clip_userflags(rig, 0x35) != 0)) {
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffdffff;
            }
            if ((anim_primary_clip_userflags(rig, 0x34) == 0) &&
                (anim_primary_clip_userflags(rig, 0x35) == 0)) {
                *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffdffff;
            }
        }
    } else {
        if (*(int *)(iVar3 + 0x94) == 1) {
            if (anim_is_clip_active(rig, 0x74) == 0) {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
            }
        } else if (*(int *)(iVar3 + 0x94) == 0x36) {
            if (anim_is_clip_active(rig, 0x111) == 0) {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
            }
        } else if (((*(uint *)(DAT_80610ba0 + *(int *)(iVar3 + 0x204) * 0x29c + 0xc) & 1) == 0) ||
                   (iVar4 = anim_is_clip_active(rig, 0x61b), iVar4 != 0)) {
            if (((*(uint *)(DAT_80610ba0 + *(int *)(iVar3 + 0x204) * 0x29c + 0xc) & 2) != 0) &&
                (anim_is_clip_active(rig, 0x60a) == 0)) {
                anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
            }
        } else {
            anim_play_scaled((double)FLOAT_80618e58, (double)FLOAT_80618e5c);
        }
        if (((anim_primary_clip_userflags(rig, 0x61b) != 0) ||
             (anim_primary_clip_userflags(rig, 0x60a) != 0)) ||
            ((anim_primary_clip_userflags(rig, 0x111) != 0) ||
             (anim_primary_clip_userflags(rig, 0x74) != 0))) {
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffffbff;
        }
        if (((anim_is_clip_active(rig, 0x61b) == 0) && (anim_is_clip_active(rig, 0x60a) == 0)) &&
            ((anim_is_clip_active(rig, 0x111) == 0) && (anim_is_clip_active(rig, 0x74) == 0))) {
            *(uint *)(DAT_80611d74 + 0xc) = *(uint *)(DAT_80611d74 + 0xc) & 0xfffffbff;
        }
    }
    FUN_8038273c();
}

// ============================================================================
// 0x80255898  player_read_move_input
// Reads the pad analog axes for the local player and produces a clamped, sine-
// remapped move vector (param_1) and aim vector (param_2). Two axis-set variants
// depending on the control mode DAT_80611d74[0x114]: the "aim" mode reads axes
// 0x14/0x15 for move and 0x12/0x13 for aim; the default reads 0x10/0x11 for move
// and 0xe/0xf for aim. Both dead-zone the move magnitude (FLOAT_80618e9c) and
// clamp to unit (FLOAT_80618e64) via the sine table, then clamp the aim vector to
// the per-frame turn-rate window (DAT_80618e40..e50) and, in mirrored aim modes,
// negate the aim-Y. Zeroes everything for the 0x22100 mode mask.
// ============================================================================
void FUN_80255898(float *param_1, float *param_2)
{
    int    iVar1;
    float  fVar2, fVar3, fVar4, fVar5;
    bool   bVar6;
    uint   uVar7;
    double dVar8, dVar9;
    double local_38;

    fVar2 = FLOAT_80618e58;
    uVar7 = *(uint *)(DAT_80611d74 + 0x114);
    if ((uVar7 & 0x22100) == 0) {
        bVar6 = false;
        if ((uVar7 == 0x80) &&
            (*(short *)(*(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0xc14) + 0x94) != 0)) {
            bVar6 = true;
        }
        if ((*(int *)(DAT_80611d74 + 0xdf4) != 0) || (uVar7 == 0x40) || (bVar6)) {
            input_set_analog_value((double)FLOAT_80618e58, *(int *)DAT_80611d74);
            if (*(int *)(*(int *)(DAT_80611d74 + 0x1398) + 0x74) == 2) {
                input_set_analog_value((double)FLOAT_80618e98, *(int *)DAT_80611d74);
            }
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x15);
            *param_1 = (float)-dVar8;
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x14);
            param_1[1] = (float)dVar8;
            input_set_analog_value((double)FLOAT_80618e78, *(int *)DAT_80611d74);
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x12);
            *param_2 = (float)dVar8;
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x13);
            param_2[1] = (float)dVar8;
            dVar9 = math_sqrt((double)(*param_1 * *param_1 + param_1[1] * param_1[1]));
            dVar8 = (double)FLOAT_80618e64;
            if ((dVar9 <= dVar8) && (dVar8 = dVar9, dVar9 < (double)FLOAT_80618e9c)) {
                dVar8 = (double)FLOAT_80618e9c;
            }
            if ((double)FLOAT_80618e58 < dVar8) {
                dVar9 = (double)math_atan2((double)param_1[1], (double)*param_1);
                uVar7 = (uint)(FLOAT_80618ea4 * (float)dVar9 + FLOAT_80618ea0);
                *param_1 = (float)(dVar8 * (double)(float)(dVar8 * (double)*(float *)((int)&DAT_80583228 +
                                                                                     ((uVar7 + 0x200) * 4 & 0x1ffc))));
                param_1[1] = (float)(dVar8 * (double)(float)(dVar8 * (double)(float)(&DAT_80583228)[uVar7 & 0x7ff]));
            }
            if ((((*(ushort *)(*(int *)(DAT_80611d74 + 0x1398) + 2) & 1) != 0) &&
                 (*(int *)(DAT_80611d74 + 0x114) != 0x40)) &&
                ((*(int *)(DAT_80611d74 + 0x114) != 0x80) || (*(int *)(DAT_80611d74 + 0xdf4) != 0))) {
                param_1[1] = -param_1[1];
            }
        } else {
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x11);
            *param_1 = (float)-dVar8;
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0x10);
            param_1[1] = (float)dVar8;
            input_set_analog_value((double)FLOAT_80618e78, *(int *)DAT_80611d74);
            dVar9 = math_sqrt((double)(*param_1 * *param_1 + param_1[1] * param_1[1]));
            dVar8 = (double)FLOAT_80618e64;
            if ((dVar9 <= dVar8) && (dVar8 = dVar9, dVar9 < (double)FLOAT_80618e9c)) {
                dVar8 = (double)FLOAT_80618e9c;
            }
            if ((double)FLOAT_80618e58 < dVar8) {
                dVar9 = (double)math_atan2((double)param_1[1], (double)*param_1);
                uVar7 = (uint)(FLOAT_80618ea4 * (float)dVar9 + FLOAT_80618ea0);
                *param_1 = (float)(dVar8 * (double)(float)(dVar8 * (double)*(float *)((int)&DAT_80583228 +
                                                                                     ((uVar7 + 0x200) * 4 & 0x1ffc))));
                param_1[1] = (float)(dVar8 * (double)(float)(dVar8 * (double)(float)(&DAT_80583228)[uVar7 & 0x7ff]));
            }
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0xe);
            *param_2 = (float)dVar8;
            dVar8 = FUN_800a4498((int *)DAT_80611d74, 0xf);
            param_2[1] = (float)dVar8;
        }
        local_38 = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        iVar1 = *(int *)(DAT_80611d74 + 0x11c) * 4;
        fVar2 = FLOAT_806108ec *
                (*(float *)(&DAT_80618e40 + iVar1) / (float)(local_38 - DOUBLE_80618e90)) +
                *(float *)(DAT_80611d74 + 0xe5c);
        fVar3 = FLOAT_806108ec *
                (*(float *)(&DAT_80618e50 + iVar1) /
                 (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90));
        fVar4 = *(float *)(DAT_80611d74 + 0xe60) + fVar3;
        fVar5 = -(FLOAT_806108ec *
                  (*(float *)(&DAT_80618e48 + iVar1) /
                   (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) -
                  *(float *)(DAT_80611d74 + 0xe5c));
        fVar3 = *(float *)(DAT_80611d74 + 0xe60) - fVar3;
        if (fVar2 < *param_2) { *param_2 = fVar2; }
        if (*param_2 < fVar5) { *param_2 = fVar5; }
        if (fVar4 < param_2[1]) { param_2[1] = fVar4; }
        if (param_2[1] < fVar3) { param_2[1] = fVar3; }
        fVar2 = FLOAT_80618e58;
        if ((*(uint *)(DAT_80611d74 + 0xc) & 0x8000) != 0) {
            *param_2 = FLOAT_80618e58;
            param_2[1] = fVar2;
        }
    } else {
        param_2[1] = FLOAT_80618e58;
        *param_2 = fVar2;
        param_1[1] = fVar2;
        *param_1 = fVar2;
    }
}

// ============================================================================
// 0x80255ca8  player_scale_move_input
// Post-scales the move (param_1) and aim (param_2) vectors produced by
// player_read_move_input: in aim mode 0x80 it zeroes the move vector and scales
// aim by FLOAT_80618ea8 (or FLOAT_80618e78 when the sighted-weapon substate is
// active, or zero when the no-turn bit is set); in the non-turret/non-aim modes
// with the crouch bit set it scales aim by FLOAT_80618eac; and in difficulty mode
// 8 (DAT_80611934) it scales all four components by a difficulty-derived factor.
// ============================================================================
void FUN_80255ca8(float *param_1, float *param_2)
{
    float  fVar1, fVar2, fVar3;
    int    iVar4;
    uint   uVar5;
    int    iVar6;
    double dVar7;

    fVar1 = FLOAT_80618e58;
    if (*(int *)(DAT_80611d74 + 0x114) == 0x80) {
        iVar4 = *(int *)(*(int *)(*(int *)(DAT_80611d74 + 0x14) + 0x7c) + 0xc14);
        iVar6 = *(int *)(iVar4 + 0x7c);
        *param_1 = FLOAT_80618e58;
        param_1[1] = fVar1;
        fVar3 = FLOAT_80618ea8;
        fVar2 = FLOAT_80618e78;
        if (*(short *)(iVar4 + 0x94) == 0) {
            *param_2 = *param_2 * FLOAT_80618ea8;
            param_2[1] = param_2[1] * fVar3;
        } else {
            uVar5 = *(uint *)(iVar6 + 8);
            if (((uVar5 & 2) == 0) || ((uVar5 & 0x400) != 0)) {
                *param_2 = *param_2 * FLOAT_80618e78;
                param_2[1] = param_2[1] * fVar2;
            } else {
                *param_2 = fVar1;
                param_2[1] = fVar1;
            }
        }
    }
    fVar1 = FLOAT_80618eac;
    iVar4 = *(int *)(DAT_80611d74 + 0x114);
    if (((iVar4 != 0x80) && (iVar4 != 0x40)) && (iVar4 != 0x400)) {
        if ((iVar4 != 0x200) && ((*(uint *)(DAT_80611d74 + 0xc) & 0x10) != 0)) {
            *param_2 = *param_2 * FLOAT_80618eac;
            param_2[1] = param_2[1] * fVar1;
        }
    }
    if (DAT_80611934 == 8) {
        dVar7 = game_get_difficulty_speed();
        fVar1 = FLOAT_80618e64 - (float)((double)FLOAT_8061192c / dVar7);
        *param_1 = *param_1 * fVar1;
        param_1[1] = param_1[1] * fVar1;
        *param_2 = *param_2 * fVar1;
        param_2[1] = param_2[1] * fVar1;
    }
}

// ============================================================================
// 0x80255e10  player_apply_aim_delta
// Integrates the aim vector (param_2 = {yawRate, pitchRate}) into the player's
// look angles held in the rig state block at (param_1->+0x7c). Each rate is
// scaled by the frame delta (FLOAT_806108ec), the camera lerp factor, and the
// per-axis sensitivity (FLOAT_80611d80/84); when the aim-inherit bit 0x1000000 is
// set the rate is additionally scaled by the object's aim-scale field (+0x5c).
// The resulting yaw (+0x1c), pitch (+0x24) and roll (+0x2c) are wrapped/clamped
// against their configured limits via angle_wrap_clamp (or snapped when lo==hi).
// ============================================================================
void FUN_80255e10(int param_1, float *param_2)
{
    float  fVar1, fVar2, fVar3;
    int    iVar5;
    double dVar6;

    iVar5 = *(int *)(param_1 + 0x7c);
    fVar3 = FLOAT_80618e64;
    if (DAT_80611d54 != 0) {
        dVar6 = player_get_camera_lerp_factor(DAT_80611d54);
        fVar3 = (float)((double)*(float *)(DAT_80611d54 + 900) / dVar6);
    }
    dVar6 = DOUBLE_80618e90;
    fVar1 = param_2[1];
    fVar2 = *param_2;
    if ((*(uint *)(iVar5 + 8) & 0x1000000) != 0) {
        fVar1 = fVar1 * *(float *)(iVar5 + 0x5c);
        fVar2 = fVar2 * *(float *)(iVar5 + 0x5c);
    }
    *(float *)(iVar5 + 0x1c) =
        -(FLOAT_806108ec *
          fVar3 * fVar1 * FLOAT_80611d80 *
          (FLOAT_80618e64 /
           (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) -
          *(float *)(iVar5 + 0x1c));
    *(float *)(iVar5 + 0x24) =
        FLOAT_806108ec *
        fVar3 * fVar2 * FLOAT_80611d84 *
        (FLOAT_80618e64 /
         (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_80618e90)) +
        *(float *)(iVar5 + 0x24);
    dVar6 = (double)*(float *)(iVar5 + 0x34);
    if (dVar6 == (double)*(float *)(iVar5 + 0x38)) {
        *(float *)(iVar5 + 0x1c) = *(float *)(iVar5 + 0x34);
    } else {
        dVar6 = angle_wrap_clamp((double)*(float *)(iVar5 + 0x1c), dVar6, (double)*(float *)(iVar5 + 0x38));
        *(float *)(iVar5 + 0x1c) = (float)dVar6;
    }
    dVar6 = (double)*(float *)(iVar5 + 0x3c);
    if (dVar6 == (double)*(float *)(iVar5 + 0x40)) {
        *(float *)(iVar5 + 0x24) = *(float *)(iVar5 + 0x3c);
    } else {
        dVar6 = angle_wrap_clamp((double)*(float *)(iVar5 + 0x24), dVar6, (double)*(float *)(iVar5 + 0x40));
        *(float *)(iVar5 + 0x24) = (float)dVar6;
    }
    dVar6 = (double)*(float *)(iVar5 + 0x44);
    if (dVar6 == (double)*(float *)(iVar5 + 0x48)) {
        *(float *)(iVar5 + 0x2c) = *(float *)(iVar5 + 0x44);
    } else {
        dVar6 = angle_wrap_clamp((double)*(float *)(iVar5 + 0x2c), dVar6, (double)*(float *)(iVar5 + 0x48));
        *(float *)(iVar5 + 0x2c) = (float)dVar6;
    }
}

} // extern "C"
