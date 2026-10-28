// slot_machine_prop.cpp
// Level-prop subsystem: 3-reel combination / slot-machine prop.
// Continuation of the prop-object cluster (batch_710-713). The pending latch
// DAT_805cad0c and the three 0x10-byte reel blocks at DAT_805cacd8 are driven
// here; prop_request_enable / prop_subsystem_reset (batch_713) arm and clear it.
//
// Reel block layout (0x10 bytes each, base DAT_805cacd8, stride 0x10):
//   +0x00 target_digit (0-9)      -> field DAT_805cacd8 / DAT_805cace8 / ...
//   +0x04 flags (bit0 = moving, bit1 = force-lock request)  DAT_805cacdc ...
//   +0x08 current_angle (deg, 0..0x167, +6/frame)           DAT_805cace0 ...
//   +0x0c target_angle  (= target_digit * 0x24)             DAT_805cace4 ...
// Angle unit: 36 deg (0x24) per digit, 360 deg (0x168) wrap.
//
// State machine (DAT_805cad0c):
//   0 idle/arm -> 1 present-locked -> 2 pre-spin settle -> 3 spinning/resolve
//   4 no-win flash -> 6 win payout flash -> 7 near-miss flash
//
// Address range: 0x80241970 - 0x802425bf

extern "C" {

// ---- shared cross-module helpers (resolve via generated headers) ----
int  rng_counter_tick(void);
void sound_init_play_params(int params);
int  sound_play_request(void *params);
void mtx4_set_identity(int mtx);
void mtx4_set_translation(double x, double y, double z);
void mtx4_multiply(void *tmp, float *dst, float *src);
void mtx4_set_rotation_zyx(double rz, double ry, double rx, float *tmp);

// ---- raw symbols referenced with file-local signatures ----
extern int   FUN_803826e8(void);   // fetch active slot-prop work context (state ctx)
extern int   FUN_803826d8(void);   // fetch active slot-prop render context (matrices)
extern void  FUN_80382734(void);    // work-context release
extern void  FUN_80382724(void);    // render-context release

// slot lifecycle/audio helpers (defined in slot_machine_lifecycle.cpp)
void slot_handle_player_input(void);     // 0x802426f4 per-frame input/lock handling
void slot_attract_animate(int ctx);      // 0x80242638 state-0 idle attract cycle
void slot_spin_loop_sfx_stop(void);      // 0x80242604 stop spin-loop SFX
void slot_spin_loop_sfx_start(void);     // 0x802425c0 start spin-loop SFX

// pending latch + reel state globals
extern int  DAT_805cad0c;  // machine state
extern int  DAT_805cad08;  // per-state frame counter
extern int  DAT_805cad1c;  // pre-spin settle / housing tilt angle
extern int  DAT_805cad24;  // re-scramble countdown
extern int  DAT_805cad28;  // enable-held flag

// reel block 0 anchors (subsequent reels via +0x10 stride)
extern int  DAT_805cacd8;  // reel[0].target_digit
extern int  DAT_805cacdc;  // reel[0].flags
extern int  DAT_805cace0;  // reel[0].current_angle
extern int  DAT_805cace4;  // reel[0].target_angle
extern int  DAT_805cacc8;  // reel-solution table (previous-frame targets, -0x10 of blocks)
extern int  DAT_805cacec;  // reel[1].flags
extern int  DAT_805cacf8;  // reel[2].target snapshot
extern int  DAT_805cacfc;  // reel[2].flags

extern int    DAT_80610f00;   // spin-loop SFX handle (-1 = none)
extern int    DAT_8046cae8;   // winning-combination table (stride 0x14, -2 wildcard, 10 term)
extern int    DAT_8046ccc8;   // reel bone-index / anim-channel table (per reel, stride 4)
extern int    DAT_8046ccd4;   // reel bone matrix slot table
extern float  FLOAT_80618968; // deg->rad-ish scale numerator
extern float  FLOAT_8061896c; // scale denominator
extern float  FLOAT_80618970; // zero angle for Y/X rotation
extern double DOUBLE_80618978; // 2^52 bias constant for int->double

// forward decls (module-local)
unsigned int slot_reel_is_settled(int reel);
void slot_reel_set_target(unsigned int *reel, unsigned int forced_digit);

// -----------------------------------------------------------------------------
// slot_reel_init (0x80242298)
// Initialise one reel block to a fresh random target digit and matching angle.
// -----------------------------------------------------------------------------
void slot_reel_init(unsigned int *reel)
{
    unsigned int digit = (unsigned int)rng_counter_tick();
    reel[0] = digit % 10;      // target_digit
    reel[1] = 0;               // flags cleared
    reel[3] = reel[0] * 0x24;  // target_angle
    reel[2] = reel[0] * 0x24;  // current_angle (starts settled)
}

// -----------------------------------------------------------------------------
// slot_reel_set_target (0x802422f0)
// Latch the "moving" flag and pick a new target digit for a reel: a forced digit
// when forced_digit != 0xffffffff, else a fresh random digit. No-op if the reel
// is already force-locked (flag bit1).
// -----------------------------------------------------------------------------
void slot_reel_set_target(unsigned int *reel, unsigned int forced_digit)
{
    reel[1] |= 1;                       // moving
    if ((reel[1] & 2) == 0) {           // not force-locked
        if (forced_digit == 0xffffffff) {
            unsigned int r = (unsigned int)rng_counter_tick();
            reel[0] = r % 10;
        } else {
            reel[0] = forced_digit;
        }
        reel[3] = reel[0] * 0x24;       // target_angle
    }
}

// -----------------------------------------------------------------------------
// slot_reel_stop (0x80242364)
// Clear the moving flag; if the reel has landed exactly on its target angle,
// play the reel-lock "click" SFX 0x3a1.
// -----------------------------------------------------------------------------
void slot_reel_stop(int reel)
{
    int local_38[14];

    *(unsigned int *)(reel + 4) &= 0xfffffffe;   // clear moving
    if (*(int *)(reel + 8) == *(int *)(reel + 0xc)) {
        sound_init_play_params((int)local_38);
        local_38[0] = 0x3a1;
        sound_play_request(local_38);
    }
}

// -----------------------------------------------------------------------------
// slot_reel_advance (0x802423b4)
// Per-frame reel spin. If force-lock (bit1) set, drop it. When not moving,
// step current_angle toward target (+6 deg, wrap 360) and click-lock on arrival.
// When moving (free-spin), just advance the angle continuously.
// -----------------------------------------------------------------------------
void slot_reel_advance(int reel)
{
    int local_38[14];

    if ((*(unsigned int *)(reel + 4) & 2) != 0) {
        *(unsigned int *)(reel + 4) &= 0xfffffffc;   // drop force-lock + moving
    }

    if ((*(unsigned int *)(reel + 4) & 1) == 0) {    // settled/decelerating
        if (*(int *)(reel + 8) != *(int *)(reel + 0xc)) {
            *(int *)(reel + 8) += 6;
            if (0x167 < *(int *)(reel + 8)) {
                *(int *)(reel + 8) -= 0x168;
            }
            if (*(int *)(reel + 8) == *(int *)(reel + 0xc)) {
                sound_init_play_params((int)local_38);
                local_38[0] = 0x3a1;
                sound_play_request(local_38);
            }
        }
    } else {                                          // free-spinning
        *(int *)(reel + 8) += 6;
        if (0x167 < *(int *)(reel + 8)) {
            *(int *)(reel + 8) -= 0x168;
        }
    }
}

// -----------------------------------------------------------------------------
// slot_reel_is_settled (0x802424b8)
// Return 1 if reel N is not moving AND its current angle equals its target.
// -----------------------------------------------------------------------------
unsigned int slot_reel_is_settled(int reel)
{
    if (((&DAT_805cacdc)[reel * 4] & 1) != 0) {
        return 0;                                     // still moving
    }
    // countLeadingZeros(cur - target) >> 5  ==  (cur == target) ? 1 : 0
    unsigned int diff = (unsigned int)(*(int *)(&DAT_805cace4 + reel * 0x10)
                                       - (&DAT_805cace0)[reel * 4]);
    unsigned int clz = 0;
    for (unsigned int m = 0x80000000u; m != 0 && (diff & m) == 0; m >>= 1) {
        clz++;
    }
    if (diff == 0) clz = 32;
    return clz >> 5;
}

// -----------------------------------------------------------------------------
// slot_reels_all_settled (0x80242468)
// Return 1 only if all three reels report settled.
// -----------------------------------------------------------------------------
int slot_reels_all_settled(void)
{
    for (int i = 0; i < 3; i++) {
        if (slot_reel_is_settled(i) == 0) {
            return 0;
        }
    }
    return 1;
}

// -----------------------------------------------------------------------------
// slot_match_combination (0x802424f4)
// Match the current 3-digit reel readout against the winning-combination table
// (DAT_8046cae8, stride 0x14, entry digit -2 = wildcard, terminator digit 10).
// Returns the matching combo index, or -1. Falls through to an all-same jackpot
// check on the caller-supplied digit snapshot when no table entry matched.
// -----------------------------------------------------------------------------
int slot_match_combination(int *snapshot)
{
    int combo_index = 0;

    for (int off = 0; *(int *)((int)&DAT_8046cae8 + off) != 10; off += 0x14) {
        bool matched = true;
        int col = 0;
        int reel_off = 0;
        int remaining = 3;
        do {
            int want = *(int *)((int)&DAT_8046cae8 + off + col);
            if ((*(int *)((int)&DAT_805cacd8 + reel_off) != want) && (want != -2)) {
                matched = false;
                break;
            }
            col += 4;
            reel_off += 0x10;
            remaining--;
        } while (remaining != 0);
        if (matched) {
            return combo_index;
        }
        combo_index++;
    }

    // jackpot: all three snapshot digits equal
    bool all_same = true;
    int elem = 4;
    int remaining = 2;
    do {
        if (*snapshot != *(int *)((int)snapshot + elem)) {
            all_same = false;
            break;
        }
        elem += 4;
        remaining--;
    } while (remaining != 0);

    if (all_same) {
        return combo_index;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// slot_reel_spin_start (0x8024206c)
// State 1/4 -> 2: begin a spin. Decrement the re-scramble countdown, count how
// many reels are already force-locked (flag bit1), then for each reel choose its
// new target: copy neighbour, copy previous solution, force-lock reel[2], or
// random (0xffffffff). Kicks the spin-start SFX 0x39e.
// -----------------------------------------------------------------------------
void slot_reel_spin_start(void)
{
    int ctx;
    int local_48[18];

    ctx = FUN_803826e8();
    if ((DAT_805cad0c == 1) || (DAT_805cad0c == 4)) {
        DAT_805cad0c = 2;
        *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x40007;
        if (DAT_805cad24 != 0) {
            DAT_805cad24 -= 1;
        }

        // count force-locked reels
        int locked = 0;
        int reel_off = 0;
        for (int i = 3; i != 0; i--) {
            locked += (*(unsigned int *)((int)&DAT_805cacdc + reel_off) >> 1) & 1;
            reel_off += 0x10;
        }

        int idx = 0;
        reel_off = 0;
        unsigned int rr;
        do {
            if (locked == 2) {
                rr = (unsigned int)rng_counter_tick();
                if (rr % 100 < 0x29) goto rescramble;
                if ((*(unsigned int *)((int)&DAT_805cacdc + reel_off) & 2) == 0) {
                    int neighbour = idx + 1;
                    if (idx != 0) {
                        neighbour = idx - 1;
                    }
                    slot_reel_set_target((unsigned int *)((int)&DAT_805cacd8 + reel_off),
                                         (&DAT_805cacd8)[neighbour * 4]);
                }
            } else {
rescramble:
                if ((DAT_805cad24 == 0) && (idx != 0)) {
                    slot_reel_set_target((unsigned int *)((int)&DAT_805cacd8 + reel_off),
                                         *(unsigned int *)((int)&DAT_805cacc8 + reel_off));
                } else if ((locked == 0) && (idx == 1) &&
                           (rr = (unsigned int)rng_counter_tick(), 0x55 < rr % 100)) {
                    slot_reel_set_target((unsigned int *)((int)&DAT_805cacd8 + reel_off),
                                         *(unsigned int *)((int)&DAT_805cacc8 + reel_off));
                } else if ((locked == 1) && ((DAT_805cacfc & 2) != 0) && (idx == 0) &&
                           (rr = (unsigned int)rng_counter_tick(), 0x4b < rr % 100)) {
                    slot_reel_set_target((unsigned int *)((int)&DAT_805cacd8 + reel_off),
                                         DAT_805cacf8);
                } else {
                    slot_reel_set_target((unsigned int *)((int)&DAT_805cacd8 + reel_off),
                                         0xffffffff);
                }
            }
            idx++;
            reel_off += 0x10;
        } while (idx < 3);

        DAT_805cad08 = 0;
        sound_init_play_params((int)local_48);
        local_48[0] = 0x39e;
        sound_play_request(local_48);
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// slot_reel_tick (0x80241970)
// Master per-frame slot-machine state machine (DAT_805cad0c). Drives the reels,
// evaluates the combination, and branches into win / no-win / near-miss payout
// states with their SFX (0x39f near-miss reject, 0x3a0 win, 0x39e stop chime).
// -----------------------------------------------------------------------------
void slot_reel_tick(void)
{
    unsigned int *puVar1;
    int ctx;
    unsigned int uVar3;
    unsigned int uVar4;
    int iVar5;
    int iVar6;
    int iVar7;
    int local_88[15];
    int local_4c[19];

    ctx = FUN_803826e8();
    slot_handle_player_input();
    switch (DAT_805cad0c) {
    case 0:
        slot_attract_animate(ctx);
        break;
    case 1:
        *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x40007;
        if (DAT_805cad28 == 0) {
            DAT_805cad0c = 0;
        }
        break;
    case 2:
        if (DAT_805cad1c < 0x60) {
            DAT_805cad1c += 6;
        } else {
            DAT_805cad0c = 3;
            slot_spin_loop_sfx_start();
            *(int *)(ctx + 0x22c) = DAT_80610f00;
        }
        break;
    case 3:
        iVar5 = 0;
        iVar7 = 0;
        iVar6 = 0;
        DAT_805cad08 += 1;
        do {
            if ((unsigned int)DAT_805cad08 == (unsigned int)(iVar6 + 0x78)) {
                slot_reel_stop((int)&DAT_805cacd8 + iVar7);
            }
            slot_reel_advance((int)&DAT_805cacd8 + iVar7);
            iVar5++;
            iVar6 += 0x78;
            iVar7 += 0x10;
        } while (iVar5 < 3);

        uVar3 = 0;
        iVar5 = ((int)DAT_805cad08 >> 3) % 3;
        if (iVar5 == 1) {
            uVar3 = 0x12480;
        } else if (iVar5 < 1) {
            if (-1 < iVar5) {
                uVar3 = 0x9240;
            }
        } else if (iVar5 < 3) {
            uVar3 = 0x24900;
        }

        uVar4 = slot_reel_is_settled(0);
        if (uVar4 != 0) uVar3 = (uVar3 & 0xfffffe3f) | 0x480;
        uVar4 = slot_reel_is_settled(1);
        if (uVar4 != 0) uVar3 = (uVar3 & 0xfffff1ff) | 0x2400;
        uVar4 = slot_reel_is_settled(2);
        if (uVar4 != 0) uVar3 = (uVar3 & 0xfffc0fff) | 0x12000;
        *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = uVar3 | 0x40007;

        if (slot_reels_all_settled() != 0) {
            slot_spin_loop_sfx_stop();
            *(int *)(ctx + 0x22c) = -1;
            iVar5 = 0;
            iVar6 = 0;
            iVar7 = 3;
            do {
                puVar1 = (unsigned int *)((int)&DAT_805cacd8 + iVar6);
                iVar6 += 0x10;
                *(unsigned int *)((int)local_88 + iVar5) = *puVar1;
                iVar5 += 4;
                iVar7--;
            } while (iVar7 != 0);

            iVar5 = slot_match_combination(local_88);
            if (iVar5 < 0) {
                DAT_805cad08 = 0;
                uVar3 = (unsigned int)rng_counter_tick();
                if (uVar3 % 100 < 0x47) {
                    DAT_805cad0c = 7;   // near-miss flash
                    *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x52487;
                } else {
                    DAT_805cad0c = 4;   // no-win flash
                    sound_init_play_params((int)(local_88 + 3));
                    local_88[3] = 0x39f;
                    sound_play_request(local_88 + 3);
                }
            } else {
                DAT_805cad0c = 6;       // win payout flash
                DAT_805cad08 = 0;
                sound_init_play_params((int)local_4c);
                local_4c[0] = 0x3a0;
                sound_play_request(local_4c);
                uVar3 = (unsigned int)rng_counter_tick();
                DAT_805cad24 = uVar3 % 10 + 5;
            }
        }
        break;
    case 4:
        *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x52480;
        if ((DAT_805cad08 >> 2 & 1) == 0) {
            uVar3 = 1;
            if ((DAT_805cacdc & 2) != 0) uVar3 = 8;
            uVar4 = 4;
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) |= uVar3;
            if ((DAT_805cacec & 2) != 0) uVar4 = 0x10;
            uVar3 = 2;
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) |= uVar4;
            if ((DAT_805cacfc & 2) != 0) uVar3 = 0x20;
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) |= uVar3;
        } else {
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) |= 0x38;
        }
        DAT_805cad08 += 1;
        break;
    case 6:
        if (0x77 < (int)DAT_805cad08) DAT_805cad0c = 1;
        if ((DAT_805cad08 >> 3 & 1) == 0) {
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x80007;
        } else {
            *(unsigned int *)(*(int *)(ctx + 4) + 0x30) = 0x92487;
        }
        DAT_805cad08 += 1;
        break;
    case 7:
        if (0x3b < (int)DAT_805cad08) DAT_805cad0c = 1;
        DAT_805cad08 += 1;
        break;
    }

    if ((DAT_805cad0c != 2) && (0 < DAT_805cad1c)) {
        DAT_805cad1c -= 2;
    }
    FUN_80382734();
}

// -----------------------------------------------------------------------------
// slot_reel_build_matrices (0x80241da0)
// Build the per-reel bone matrices for rendering: identity base, then for each of
// the three reels rotate its bone about its pivot by (current_angle scaled to
// radians) using a Z-Y-X rotation, plus the housing tilt from DAT_805cad1c.
// Marks the render context dirty (bit 0x200000).
// -----------------------------------------------------------------------------
void slot_reel_build_matrices(void)
{
    int rctx;
    float *pfVar2;
    int *piVar3;
    int iVar4;
    int iVar5;
    int iVar6;
    float *pfVar7;
    float afStack_a8[16];
    unsigned int local_68;
    unsigned int uStack_64;

    rctx = FUN_803826d8();
    mtx4_set_identity(*(int *)(*(int *)(rctx + 4) + 0x10));

    iVar4 = 0;
    for (iVar5 = 0; iVar5 < *(int *)(*(int *)(*(int *)(rctx + 4) + 4) + 4); iVar5++) {
        mtx4_set_identity(*(int *)(*(int *)(rctx + 4) + 0x10) + iVar4);
        iVar4 += 0x40;
    }

    double scaleNum = (double)FLOAT_80618968;
    double scaleDen = (double)FLOAT_8061896c;
    iVar6 = 0;
    iVar5 = 0;
    iVar4 = 0;
    double bias = DOUBLE_80618978;
    do {
        piVar3 = *(int **)(*(int *)(rctx + 4) + 4);
        pfVar7 = (float *)(piVar3[*piVar3 * -0x30
                           + ((int)*(unsigned int *)((int)&DAT_8046ccc8 + iVar4) >> 0x10) * 0x30
                           + 0x1b]
                           + (*(unsigned int *)((int)&DAT_8046ccc8 + iVar4) & 0xffff) * 0x18);
        mtx4_set_translation((double)pfVar7[0], (double)pfVar7[1], (double)pfVar7[2]);

        pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10)
                           + *(int *)((int)&DAT_8046ccd4 + iVar4) * 0x40);
        mtx4_multiply(afStack_a8, pfVar2, pfVar2);

        local_68 = 0x43300000;
        uStack_64 = *(unsigned int *)((int)&DAT_805cace0 + iVar5) ^ 0x80000000;
        {
            double angleDeg = (double)*(double *)&local_68 - bias; // int->double via magic bias
            double rz = (double)(float)((double)(float)(scaleNum
                        * (double)(float)(angleDeg)) / scaleDen);
            mtx4_set_rotation_zyx(rz, (double)FLOAT_80618970, (double)FLOAT_80618970, afStack_a8);
        }

        pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10)
                           + *(int *)((int)&DAT_8046ccd4 + iVar4) * 0x40);
        mtx4_multiply(afStack_a8, pfVar2, pfVar2);

        mtx4_set_translation(-(double)pfVar7[0], -(double)pfVar7[1], -(double)pfVar7[2]);
        pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10)
                           + *(int *)((int)&DAT_8046ccd4 + iVar4) * 0x40);
        mtx4_multiply(afStack_a8, pfVar2, pfVar2);

        iVar6++;
        iVar4 += 4;
        iVar5 += 0x10;
    } while (iVar6 < 3);

    // housing tilt (reel[0..2] frame) using DAT_805cad1c
    piVar3 = *(int **)(*(int *)(rctx + 4) + 4);
    pfVar7 = (float *)piVar3[*piVar3 * -0x30 + 0x1b];
    mtx4_set_translation((double)pfVar7[0], (double)pfVar7[1], (double)pfVar7[2]);
    pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10) + 0x40);
    mtx4_multiply(afStack_a8, pfVar2, pfVar2);

    local_68 = 0x43300000;
    uStack_64 = DAT_805cad1c ^ 0x80000000;
    {
        double angleDeg = (double)*(double *)&local_68 - DOUBLE_80618978;
        double rz = (double)((FLOAT_80618968 * (float)(angleDeg)) / FLOAT_8061896c);
        mtx4_set_rotation_zyx(rz, (double)FLOAT_80618970, (double)FLOAT_80618970, afStack_a8);
    }
    pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10) + 0x40);
    mtx4_multiply(afStack_a8, pfVar2, pfVar2);
    mtx4_set_translation(-(double)pfVar7[0], -(double)pfVar7[1], -(double)pfVar7[2]);
    pfVar2 = (float *)(*(int *)(*(int *)(rctx + 4) + 0x10) + 0x40);
    mtx4_multiply(afStack_a8, pfVar2, pfVar2);

    *(unsigned int *)(rctx + 0x28) |= 0x200000;
    FUN_80382724();
}

} // extern "C"
