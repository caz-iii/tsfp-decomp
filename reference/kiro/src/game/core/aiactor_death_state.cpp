// aiactor_death_state.cpp
// AI actor death / dying state machine and related target/voice helpers,
// recovered in batch_810. The main tick (aiactor_death_state_tick) advances the
// per-actor death state stored in the behavior block (actor+0x7c) field [8],
// driving ragdoll/animation, weapon drop, death SFX and the death effect.
//
// NON-MATCHING decompilation.

#include "common.h"

// ---------------------------------------------------------------------------
// Per-file externs for raw ROM symbols (types match the call sites).
// ---------------------------------------------------------------------------
extern float  FLOAT_80619c4c;   // 0.0f timer reset value
extern float  FLOAT_80619c50;   // 1.0f rate constant
extern float  FLOAT_80619c38;   // death anim blend rate
extern float  FLOAT_80619c78;   // fade-in duration
extern float  FLOAT_806108ec;   // frame-time scale
extern double DOUBLE_80619c68;  // int->double bias (0x43300000 pair)
extern unsigned int DAT_806109c0;   // frame delta (raw)
extern int    DAT_80611b8c;     // soundpad/zone record array base
extern int    DAT_806118dc;     // ragdoll-enabled global flag
extern char   DAT_805015bd;     // sub-mode selector ('\n' == head-to-head)
extern int    DAT_804d34dc;     // class-flags table base
extern int    DAT_804d1cd0;     // class-id -> flags-row index table (short)

// ---- named cross-module helpers (declared here; TU includes only common.h) ----
extern int  aiactor_retarget_visible_teammate(void);
extern int  anim_primary_clip_userflags(int actor, int clipId);
extern void anim_play_scaled(double a, double b);
extern void anim_clear_render_scale(int block);
extern int  anim_is_clip_active(int actor, int clipId);
extern void character_death_effect_trigger(int *fxBlock);
extern int  character_opacity_fade_in(double dur, int fxBlock);
extern void aiactor_movement_tick(double a, double b, double c, double d,
                                  double e, double f, double g, unsigned long long h);
extern void player_ai_drop_all_weapons(unsigned long long r3, double a, double b, double c,
                                       double d, double e, double f, unsigned long long g, int *block);
extern int  sound_init_play_params(int params);
extern unsigned int sound_play_request(int *params);
extern int  player_ai_char_def_is_female(int charDef);
extern int  rng_counter_tick(void);

extern float  FLOAT_80619c88;   // hold-target max age
extern int    DAT_80611b9c;     // grab/hold record array base (stride 0x2c)
extern float  FLOAT_80619d3c;   // female-voice: probability base
extern float  FLOAT_80619d34;   // female-voice: rng normaliser
extern double DOUBLE_80619d68;  // int->double bias
extern float  FLOAT_80619d78;   // female-voice: threshold

// ---------------------------------------------------------------------------
// 0x802bdaf8  aiactor_death_state_tick
// Per-frame death/dying state machine for an AI actor. 'actor' is the entity
// block; the behavior block lives at *(actor+0x7c). field[8] is the current
// death state (0=killed, 1/2=dying variants, 3=ragdoll settle). The routine:
//  - commits any pending state transition queued in field[0x274],
//  - accrues a death timer (field[0x2a4]) while flag 0x80 is set,
//  - copies the actor's current zone triplet from the zone record,
//  - and dispatches per state: play death anim, drop weapons, trigger effect,
//    emit the death SFX (0x1565), fade opacity.
// The leading double params are the PPC ABI spill prefix, forwarded to the
// movement/weapon-drop helpers.
// ---------------------------------------------------------------------------
void aiactor_death_state_tick(unsigned long long p1, double p2, double p3, double p4,
                              double p5, double p6, double p7, unsigned long long p8, int actor)
{
    int *bb = *(int **)(actor + 0x7c);
    double moveArg = (double)aiactor_retarget_visible_teammate();

    // Commit queued state transition.
    if (-1 < bb[0x274]) {
        if (bb[0x274] != bb[8]) {
            bb[0x276] = (int)FLOAT_80619c4c;
            bb[0x275] = bb[8];
            bb[8] = bb[0x274];
        }
        bb[0x274] = -1;
    }

    // Death timer accrual (flag 0x80). The original forms the frame-delta as a
    // signed-int->double via the 0x43300000 magic pair; expressed here directly.
    if ((bb[0xb] & 0x80U) == 0) {
        bb[0x2a4] = (int)FLOAT_80619c4c;
    } else {
        double norm = (double)(float)((double)(int)DAT_806109c0);
        double rate = (double)(float)((double)FLOAT_80619c50 / norm);
        bb[0x2a4] = (int)(float)((double)FLOAT_806108ec * rate + (double)(float)bb[0x2a4]);
    }

    // Copy active zone triplet.
    bb[0xc] = *(int *)(DAT_80611b8c + bb[0x19e] * 0x20 + 0xc);
    bb[0xd] = *(int *)(DAT_80611b8c + bb[0x19e] * 0x20 + 0x10);
    bb[0xe] = *(int *)(DAT_80611b8c + bb[0x19e] * 0x20 + 0x14);

    int state = bb[8];

    if (state == 2) {
        if ((bb[0x31b] & 2U) != 0) {
            if (anim_primary_clip_userflags(actor, 0xbfc) == 0) {
                return;
            }
            anim_play_scaled((double)FLOAT_80619c4c, (double)FLOAT_80619c50);
            return;
        }
        *(unsigned int *)(actor + 0x28) &= 0xfffffffe;
        *(unsigned int *)(actor + 0x10) &= 0xffff0000;
        if (DAT_806118dc == 0) {
            return;
        }
        anim_clear_render_scale((int)bb);
        if (bb[8] == 3) {
            return;
        }
        bb[0x275] = bb[8];
        bb[8] = 3;
        bb[0x276] = (int)FLOAT_80619c4c;
        bb[0x274] = bb[8];
        return;
    }

    if (state < 2) {
        if (state == 0) {
            if ((bb[0x31b] & 4U) != 0) {
                character_death_effect_trigger(bb + 0x31a);
            }
            if ((bb[0x2ad] & 2U) != 0) {
                if (bb[8] == 2) {
                    return;
                }
                bb[0x275] = bb[8];
                bb[8] = 2;
                bb[0x276] = (int)FLOAT_80619c4c;
                bb[0x274] = bb[8];
                return;
            }
            if (bb[8] == 1) {
                return;
            }
            bb[0x275] = bb[8];
            bb[8] = 1;
            bb[0x276] = (int)FLOAT_80619c4c;
            bb[0x274] = bb[8];
            return;
        }
        if (state < 0) {
            return;
        }
        aiactor_movement_tick(moveArg, p2, p3, p4, p5, p6, p7, p8);
        if (anim_is_clip_active(actor, 0xbfc) == 0) {
            anim_play_scaled((double)FLOAT_80619c38, (double)FLOAT_80619c50);
        }
        *(unsigned int *)(actor + 0x28) &= 0xffffff9f;
        if ((anim_primary_clip_userflags(actor, 0xbfc) == 0) &&
            ((*(unsigned int *)(&DAT_804d34dc +
              (short)(&DAT_804d1cd0)[*(int *)(*(int *)(actor + 4) + 0xcc)] * 8) & 0x20) == 0)) {
            return;
        }
        {
            int fade = character_opacity_fade_in((double)FLOAT_80619c78, (int)(bb + 0x31a));
            if (bb[8] != 2) {
                bb[0x275] = bb[8];
                bb[8] = 2;
                bb[0x276] = (int)FLOAT_80619c4c;
                bb[0x274] = bb[8];
            }
            player_ai_drop_all_weapons((unsigned long long)(unsigned int)fade, p2, p3, p4, p5, p6, p7, p8, bb);
        }
        return;
    }

    if (3 < state) {
        return;
    }

    // state == 3: ragdoll settle -> death SFX + weapon drop.
    *(unsigned int *)(actor + 0x28) |= 1;
    *(unsigned int *)(actor + 0x28) |= 0x60;
    int params[12];
    sound_init_play_params((int)params);
    params[0] = 0x1565;
    sound_play_request(params);
    if (bb[0x2ab] == 1 && bb[0x2aa] == 0) {
        unsigned int slot = (DAT_805015bd == '\n') ? (unsigned int)bb[5] : (unsigned int)bb[7];
        if ((slot & 1) == 0) {
            player_ai_drop_all_weapons(0, p2, p3, p4, p5, p6, p7, p8, bb);
            return;
        }
    }
    bb[0x2ab] = 2;
    bb[0x2aa] = 0;
    player_ai_drop_all_weapons(0, p2, p3, p4, p5, p6, p7, p8, bb);
}

// ---------------------------------------------------------------------------
// 0x802bdee4  aiactor_get_active_hold_target
// Returns the entity currently held/grabbed by 'actor' if it is a valid, live,
// recently-updated hold target; 0 otherwise. Reads the grab record from
// DAT_80611b9c[holdIndex] (stride 0x2c), rejecting held targets whose behavior
// flags (+0xc6c) hit mask 0x1c10, stale age (+0x14 > FLOAT_80619c88), or missing
// the "grabbable" flag (bit 0 of target+0x28).
// ---------------------------------------------------------------------------
int aiactor_get_active_hold_target(int actor)
{
    int bb = *(int *)(actor + 0x7c);
    if ((*(unsigned int *)(bb + 0xc6c) & 0x1c10) != 0) {
        return 0;
    }
    int holdIdx = *(int *)(bb + 0x678);
    if (holdIdx < 0) {
        return 0;
    }
    int rec = DAT_80611b9c + holdIdx * 0x2c;
    int held = *(int *)(rec + 8);
    if ((held == 0) || (*(int *)(held + 4) == 0)) {
        return 0;
    }
    if (FLOAT_80619c88 < *(float *)(rec + 0x14)) {
        return 0;
    }
    if ((*(unsigned int *)(*(int *)(held + 4) + 0x28) & 1) != 0) {
        return held;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802c3464  aiactor_death_voice_gate
// Probability gate for playing a death voice/reaction for 'charDef' on 'actor'.
// Female character definitions have an extra suppression roll; the final gate
// scales an rng sample by |actor.behavior+0x9fc| and compares to a threshold.
// Returns true if the voice should play. Called from object_melee_thrown_fire.
// ---------------------------------------------------------------------------
bool aiactor_death_voice_gate(int actor, int charDef)
{
    int bb = *(int *)(actor + 0x7c);

    if (player_ai_char_def_is_female(charDef) != 0) {
        int roll = rng_counter_tick();
        float r = (float)((double)(int)roll) / FLOAT_80619d34;
        if ((int)(FLOAT_80619d3c + r) == 0) {
            return false;
        }
    }

    int roll2 = rng_counter_tick();
    float r2 = (float)((double)(int)roll2) / FLOAT_80619d34;
    return (r2 * ABS(*(float *)(bb + 0x9fc))) < FLOAT_80619d78;
}
