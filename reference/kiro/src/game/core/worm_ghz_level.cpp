// worm_ghz_level.cpp
// GHZ reactor set-piece LEVEL DRIVER (address range 0x80210aa8 - 0x802117af,
// batch_656). This is the top-level orchestration layer for the GHZ reactor
// level whose per-effect primitives live in worm_ghz_state.cpp. It owns the
// per-frame master tick, the level init/reset, the resource preload table, the
// scripted-event handlers, and the particle-def/emitter construction.
// subsystem:game, worm-effect module.
//
// The whole level is gated by the global mode flag DAT_805015c0 & 0x40 (set
// during cutscene / loading; when clear the interactive set-piece runs). The
// per-frame master (ghz_level_master_tick) drives the ambient audio trigger,
// the electric drop-bolt master, the reactor arc-emitter, and (interactive
// only) the weather ramp -> bot event handler -> difficulty dispatch chain,
// finally blocking three scene portals. The level reset
// (ghz_level_reset) re-arms the sound state, the MTHS HUD event dispatcher, the
// drop-trail rig, and flags a fixed list of soundpad zones. The particle setup
// (ghz_level_particle_setup) builds the 5 spark/trail/missile particle
// def+emitter pairs (def blocks DAT_805c6884..6bb4).
//
// Functions:
//   ghz_class6_damage_gate_init (0x80210aa8) class-6 event -> set the OUTY
//                            damage-gate render callback (ghz_outy_damage_gate),
//                            return -1 to consume the event.
//   ghz_bot_weapon_swap_events (0x80210ae8) 'RBS1'/'SD01' bot anim-clip event
//                            handler (clips 0xa1c / 0xa03): swap weapon slots
//                            and play the swap sounds.
//   ghz_level_preload        (0x80210ce4) preload ~22 entity classes
//                            (0x51a..0x541) + clear 17 sound-bank refs.
//   ghz_weather_ramp_tick    (0x80210e40) ramp the weather/storm intensity by
//                            the effect-progress ratio, apply camera shake, and
//                            jitter the per-bot rain fields (0x141c stride).
//   ghz_effect_progress_reset (0x80210fc0) reset FLOAT_80612800 to the base.
//   ghz_ambient_sound_trigger (0x80210fcc) mode-keyed ambient one-shot: play
//                            sound 0x1627 / 0x10 / 0x12 by DAT_80612614.
//   ghz_difficulty_payload_gate (0x8021106c) if the 'OMBH' actor count meets a
//                            difficulty threshold, fire payload op 5.
//   ghz_level_master_tick    (0x802110f4) per-frame master: ambient trigger +
//                            bolt master + arc emitter; interactive-only weather
//                            ramp -> bot events -> difficulty gate + portal block.
//   ghz_level_reset          (0x80211160) level reset: sound state, MTHS HUD
//                            dispatcher, drop-trail reset, zone-flag list.
//   ghz_level_particle_setup (0x80211310) build 5 particle def+emitter pairs
//                            (spark burst, ground trail, missile trail, etc.).
//
// Non-matching: 4CC ids ('RBS1' 0x52425331, 'SD01' 0x53443031, 'OMBH'
// 0x4f4d4248, 'MTHS' via ghz_mths_event_dispatch); the packed BE colour/size
// keyframe literals are passed as the raw ints the ROM stores; int<->double
// bias math is written as plain float math; wide FP-ABI helpers route through
// the same file-local aliases established in worm_ghz_state.cpp. The GHZ effect
// primitives (ghz_bolt_master_tick, ghz_reactor_arc_emitter_tick,
// ghz_bolt_state_reset, ghz_droptrail_reset, ghz_mths_event_dispatch,
// ghz_outy_damage_gate, ghz_effect_progress_reset) are defined in
// worm_ghz_state.cpp / this file and referenced by name.
#include "common.h"
#include "entity.h"   // pulls generated_decls.h

extern "C" {

// --- cross-module GHZ effect primitives + level-script helpers all resolve
//     through the generated headers (generated_decls.h via entity.h):
//       ghz_bolt_master_tick, ghz_reactor_arc_emitter_tick, ghz_bolt_state_reset,
//       ghz_droptrail_reset, ghz_outy_damage_gate, ghz_mths_event_dispatch,
//       ghz_effect_progress_ratio, aiactor_find_bot_by_linked_entity,
//       anim_event_scan_active, anim_active_clip_slot, object_* helpers,
//       entity_preload_resources, entity_set_render_type, scene_portal_block,
//       weather_set_intensity, player_broadcast_camera_shake,
//       soundpad_find_zone_by_id, sound_clear_bank_ref, sound_init_play_params,
//       sound_play_request, particle_*.  Do NOT re-declare (C2371/C2373/C2732).

// --- raw data symbols (file-local) ---
extern int   DAT_805015c0;      // global mode/state flag word (0x40 = cutscene/loading gate)
extern char  DAT_805015bd;      // single-player difficulty/language byte
extern int   DAT_80610e4c;      // ambient sound handle (-1 = none playing)
extern int   DAT_80612610;      // active actor count
extern int   DAT_80612614;      // active game-mode id
extern int   DAT_80611d74;      // local/view actor block base
extern int   DAT_80611d78;      // active actor/character block base
extern int   DAT_80611d54;      // primary player/view block base
extern int   DAT_80611b8c;      // soundpad zone table base (0x20 stride)
extern int   DAT_80611d70;      // (reserved GHZ level slot)

// effect-progress + weather tuning
extern float FLOAT_80612800;    // effect-progress timer (drives weather ramp)
extern float FLOAT_80617e90;    // 0.0f family (base/seed)
extern float FLOAT_80617e8c;    // health-ratio / phase scale
extern float FLOAT_80617e9c;    // velocity/size gain
extern float FLOAT_80617ea0;    // 1.0f family
extern float FLOAT_80617ea4;    // drag / size scalar
extern float FLOAT_80617ea8;    // clamp max
extern float FLOAT_80617ec4;    // sine-LUT phase gain
extern float FLOAT_80617ec8;    // amplitude
extern float FLOAT_80617ed0;    // angle divisor / small size
extern float FLOAT_80617ed8;    // 2*PI family (rotation divisor)
extern float FLOAT_80617edc;    // amplitude base
extern float FLOAT_80617ef0;    // count/limit gate
extern float FLOAT_80617ef4;    // spin range
extern float FLOAT_80617ef8;    // rng span scalar
extern float FLOAT_80617efc;    // segment divisor
extern float FLOAT_80617f2c;    // billboard scale
extern float FLOAT_80617f40;    // effect duration / idle timeout
extern float FLOAT_80617f58;    // debris spread
extern float FLOAT_80617f5c;    // size keyframe field
extern float FLOAT_80617f9c;    // drag B
extern float FLOAT_80617fb8;    // colour-end field
extern float FLOAT_80617fbc;    // rng scalar / size seed
extern float FLOAT_80617fc4;    // spawn radius scale
extern float FLOAT_80617fcc;    // gravity / camera-shake base
extern float FLOAT_80617ff4;    // weather intensity scale / gravity B
extern float FLOAT_80617ffc;    // rain field B
extern float FLOAT_80617ff8;    // rain field base
extern float FLOAT_80617ee0;    // drag A / interp
extern float FLOAT_80617ee4;    // rng centre (0.5f)

// batch_656 particle tuning float pool 0x80618000..0x8061802c
extern float FLOAT_80618000; extern float FLOAT_80618004; extern float FLOAT_80618008;
extern float FLOAT_8061800c; extern float FLOAT_80618010; extern float FLOAT_80618014;
extern float FLOAT_80618018; extern float FLOAT_8061801c; extern float FLOAT_80618020;
extern float FLOAT_80618024; extern float FLOAT_80618028; extern float FLOAT_8061802c;

extern float FLOAT_80617f14;    // camera-shake ramp field
extern float FLOAT_80617edc;    // (declared above)

// particle def/emitter blocks (BSS, runtime-populated)
extern int   DAT_805c65c4;      // drop-trail emitter def (shared)
extern int   DAT_805c6674;      // per-bone emitter def (shared)
extern int   DAT_805c66f8;      // center emitter def (shared)
extern int   DAT_805c677c;      // missile ground-trail def (shared)
extern int   DAT_805c6800;      // 0x538-prop emitter def (shared)
extern int   DAT_805c6884;      // spark def A (this level)
extern int   DAT_805c6950;      // spark def B
extern int   DAT_805c6a1c;      // spin-spark def C
extern int   DAT_805c6ae8;      // trail def D
extern int   DAT_805c6bb4;      // trail def E
extern int   DAT_80612808;      // cached emitter handle A
extern int   DAT_80612804;      // cached emitter handle B

// emitter-scale seed vectors (0x803dfee0..0x803dfef4)
extern int   DAT_803dfee0; extern int DAT_803dfee4; extern int DAT_803dfee8;
extern int   DAT_803dfeec; extern int DAT_803dfef0; extern int DAT_803dfef4;

// --- helpers reached by raw address (file-local externs; not in generated_decls) ---
extern int  FUN_803826f0(void);        // FP spill frame open
extern void FUN_8038273c(void);        // FP spill frame close
extern int  FUN_8025bdb8(int classWord);           // count actors by 4CC
extern int  FUN_80221074(double a, double b, double c, double d,
                         double e, double f, double g, double h); // fire scripted payload
extern int  FUN_802217d4(int* renderCb);           // register HUD render callback

// rng_counter_tick is declared void in generated_decls but the ROM returns the
// 32-bit LCG counter in r3.
typedef unsigned int (*GhzRngFn)(void);
static const GhzRngFn __ghz_rng = (GhzRngFn)&rng_counter_tick;

// anim_event_scan_active is declared (int,u16)->void in generated_decls, but the
// ROM call takes no args and returns the fired-event count in r3.
typedef int (*GhzAnimScanFn)(void);
static const GhzAnimScanFn __ghz_anim_event_scan = (GhzAnimScanFn)&anim_event_scan_active;

// ghz_effect_progress_ratio / ghz_outy_damage_gate are declared in BOTH
// generated_decls.h (proper sig) and generated_externs.h (permissive int(...)),
// so a direct call / address-of is ambiguous. Bind their ROM addresses through
// typed aliases (both defined in worm_ghz_state.cpp).
typedef double (*GhzProgressFn)(void);
static const GhzProgressFn __ghz_effect_progress_ratio = (GhzProgressFn)0x8020e788;
typedef void  (*GhzOutyGateFn)(int* eventData);
static const GhzOutyGateFn __ghz_outy_damage_gate = (GhzOutyGateFn)0x80210a4c;

// frame-time bias constants (shared with worm_ghz_state.cpp; declared here for TU).
extern int    DAT_806109c0;     // per-frame time unit
extern float  DAT_806108ec;     // frame-time scale
extern double DOUBLE_80617eb0;  // 2^52 unsigned int->double bias
extern double DOUBLE_80617f38;  // 2^52 unsigned int->double bias (rng path)
extern float  FLOAT_80617f18;   // 2^32 rng divisor

// ghz_effect_progress_ratio resolves through generated_decls (double ret).

// forward decls (mutual references within this TU / registered by address).
void ghz_effect_progress_reset(void);
void ghz_weather_ramp_tick(void);
void ghz_ambient_sound_trigger(void);
void ghz_level_particle_setup(void);
int  ghz_bot_weapon_swap_events(unsigned long long p1, unsigned long long p2, unsigned long long p3,
                                unsigned long long p4, unsigned long long p5, unsigned long long p6,
                                unsigned long long p7, unsigned long long p8);
int  ghz_difficulty_payload_gate(unsigned long long p1, double p2, double p3, double p4,
                                 double p5, double p6, double p7, double p8);

// ---------------------------------------------------------------------------
// 0x80210aa8  ghz_class6_damage_gate_init
// On object event 6, install the OUTY damage-gate render callback and return -1
// to consume the event; otherwise return 0.
// ---------------------------------------------------------------------------
int ghz_class6_damage_gate_init(int entity, int event)
{
    int result = 0;
    if (event == 6) {
        entity_set_render_type(entity, (int)(intptr_t)__ghz_outy_damage_gate);
        result = -1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x80210ae8  ghz_bot_weapon_swap_events
// Scripted anim-event handler for two named bots: 'RBS1' (0x52425331) and 'SD01'
// (0x53443031). On the relevant anim clips (0xa1c for RBS1, 0xa03 for SD01) it
// swaps the bot's weapon slot and plays the swap sounds via the local player's
// sound block (DAT_80611d74+0x14 -> +0x7c).
// ---------------------------------------------------------------------------
int ghz_bot_weapon_swap_events(unsigned long long param_1, unsigned long long param_2,
                               unsigned long long param_3, unsigned long long param_4,
                               unsigned long long param_5, unsigned long long param_6,
                               unsigned long long param_7, unsigned long long param_8)
{
    int bot;
    int localSnd;
    (void)param_1;

    FUN_803826f0();

    bot = aiactor_find_bot_by_linked_entity(0x52425331);  // 'RBS1'
    if (bot != 0) {
        if (__ghz_anim_event_scan() != 0 &&
            *(int*)anim_active_clip_slot(bot) == 0xa1c) {
            int botObj = *(int*)(bot + 0x7c);
            localSnd = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
            object_sound_slot_add_scaled(localSnd, 0x19, 8);
            object_slot_switch_notify((unsigned long long)(unsigned int)localSnd, param_2, param_3,
                                      param_4, param_5, param_6, param_7, param_8);
            object_set_weapon_slot(botObj, 0x19);
            object_slot_deselect();
        }
        if (__ghz_anim_event_scan() != 0 &&
            *(int*)anim_active_clip_slot(bot) == 0xa1c) {
            object_set_weapon_slot(*(int*)(bot + 0x7c), 0x1b);
            object_slot_deselect();
        }
    }

    bot = aiactor_find_bot_by_linked_entity(0x53443031);  // 'SD01'
    if (bot != 0) {
        if (__ghz_anim_event_scan() != 0 &&
            *(int*)anim_active_clip_slot(bot) == 0xa03) {
            object_set_weapon_slot(*(int*)(bot + 0x7c), 0x20);
            object_slot_deselect();
        }
        if (__ghz_anim_event_scan() != 0 &&
            *(int*)anim_active_clip_slot(bot) == 0xa03) {
            localSnd = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
            object_set_weapon_slot(*(int*)(bot + 0x7c), 0);
            object_slot_deselect();
            object_sound_slot_add_scaled(localSnd, 0x20, 6);
            object_slot_switch_notify((unsigned long long)(unsigned int)localSnd, param_2, param_3,
                                      param_4, param_5, param_6, param_7, param_8);
            object_set_weapon_slot(localSnd, 0x20);
        }
        if (__ghz_anim_event_scan() != 0 &&
            *(int*)anim_active_clip_slot(bot) == 0xa03) {
            object_set_weapon_slot(*(int*)(bot + 0x7c), 0x19);
            object_slot_deselect();
        }
    }

    FUN_8038273c();
    return 0;
}

// ---------------------------------------------------------------------------
// 0x80210ce4  ghz_level_preload
// Level resource preload: chain entity_preload_resources for the GHZ reactor's
// entity classes (0x51a..0x541, incl. the missile/debris/prop set), then clear
// the 17 sound-bank references the level uses.
// ---------------------------------------------------------------------------
void ghz_level_preload(unsigned long long param_1, unsigned long long param_2,
                       unsigned long long param_3, unsigned long long param_4,
                       unsigned long long param_5, unsigned long long param_6,
                       unsigned long long param_7, unsigned long long param_8)
{
    static const int classes[] = {
        0x51a, 0x51d, 0x51e, 0x51f, 0x526, 0x52b, 0x52d, 0x52e, 0x52f, 0x530,
        0x531, 0x532, 0x533, 0x534, 0x535, 0x538, 0x53e, 0x51b, 0x745, 0x539,
        0x537, 0x541, 0x53a
    };
    static const int banks[] = {
        0x13f9, 0xd30, 0x13f7, 0x13f0, 0x13f1, 0x13f2, 0x13f3, 0x13f4, 0x13f5,
        0x193, 0x194, 0x186, 0x188, 0x189, 0x18a, 0x18b, 0x18c
    };
    int i;
    (void)param_2; (void)param_3; (void)param_4; (void)param_5;
    (void)param_6; (void)param_7; (void)param_8;

    for (i = 0; i < (int)(sizeof(classes) / sizeof(classes[0])); i = i + 1) {
        entity_preload_resources(classes[i]);   // ROM: single-arg (r3) each call
    }
    for (i = 0; i < (int)(sizeof(banks) / sizeof(banks[0])); i = i + 1) {
        sound_clear_bank_ref(banks[i]);
    }
}

// ---------------------------------------------------------------------------
// 0x80210e40  ghz_weather_ramp_tick
// Ramp the weather/storm intensity toward the effect-progress ratio; below the
// base it snaps to the floor, above it scales by the ratio, applies a camera
// shake, advances the effect timer, and jitters each active bot's rain fields
// (+0xe54/+0xe58, 0x141c stride) above a height threshold.
// ---------------------------------------------------------------------------
void ghz_weather_ramp_tick(void)
{
    unsigned int r;
    int i;
    int off;
    int bot;
    double ratio;
    double intensity;

    FUN_803826f0();

    if ((double)FLOAT_80612800 <= (double)FLOAT_80617e90) {
        weather_set_intensity((double)FLOAT_80617e90, 0);
    } else {
        ratio = __ghz_effect_progress_ratio();
        intensity = (double)(float)((double)FLOAT_80617ff4 * ratio);
        weather_set_intensity(intensity, 0);
        if ((DAT_805015bd == '\n') && (1 < DAT_80612610)) {
            weather_set_intensity(intensity, 1);
        }
        player_broadcast_camera_shake((double)FLOAT_80617fcc,
                                      (double)(float)((double)FLOAT_80617f14 * ratio),
                                      (double)(float)((double)FLOAT_80617edc * ratio));
        // effect timer ramp toward 0 (frame-scaled bias math preserved as float)
        FLOAT_80612800 = -((FLOAT_80617ea0 /
            (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80617eb0)) *
            DAT_806108ec - FLOAT_80612800);

        off = 0;
        for (i = 0; i < DAT_80612610; i = i + 1) {
            bot = DAT_80611d78 + off;
            if (*(int*)(bot + 0x14) != 0 &&
                FLOAT_80617e90 < *(float*)(*(int*)(bot + 0x14) + 0x1cc)) {
                r = __ghz_rng();
                *(float*)(bot + 0xe54) =
                    FLOAT_80617ee4 *
                    ((float)((double)(int)(unsigned int)r - DOUBLE_80617f38) / FLOAT_80617f18) +
                    FLOAT_80617ff8;
                *(float*)(bot + 0xe58) = FLOAT_80617ffc;
            }
            off = off + 0x141c;
        }
    }

    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80210fc0  ghz_effect_progress_reset
// Reset the effect-progress timer to its base value.
// ---------------------------------------------------------------------------
void ghz_effect_progress_reset(void)
{
    FLOAT_80612800 = FLOAT_80617e90;
}

// ---------------------------------------------------------------------------
// 0x80210fcc  ghz_ambient_sound_trigger
// When no ambient sound is playing (DAT_80610e4c < 0), play a mode-keyed one-shot:
// DAT_80612614 == 0x40 -> sound 0x1627; == 2 -> 0x10; else -> 0x12.
// ---------------------------------------------------------------------------
void ghz_ambient_sound_trigger(void)
{
    int paramsA[12];
    int paramsB[12];
    int paramsC[14];

    if ((int)DAT_80610e4c < 0) {
        if (DAT_80612614 == 0x40) {
            sound_init_play_params((int)paramsC);
            paramsC[0] = 0x1627;
            DAT_80610e4c = sound_play_request(paramsC);
        } else if (DAT_80612614 < 0x40 && DAT_80612614 == 2) {
            sound_init_play_params((int)paramsB);
            paramsB[0] = 0x10;
            DAT_80610e4c = sound_play_request(paramsB);
        } else {
            sound_init_play_params((int)paramsA);
            paramsA[0] = 0x12;
            DAT_80610e4c = sound_play_request(paramsA);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8021106c  ghz_difficulty_payload_gate
// If the 'OMBH' (0x4f4d4248) actor count meets the difficulty-scaled threshold
// (difficulty DAT_805015bd: 1 -> 0xf, 0 -> 7, <3 -> 0x19, else 10), fire the
// scripted payload op 5.
// ---------------------------------------------------------------------------
int ghz_difficulty_payload_gate(unsigned long long param_1, double param_2, double param_3,
                                double param_4, double param_5, double param_6, double param_7,
                                double param_8)
{
    int count;
    int threshold;
    (void)param_1;

    count = FUN_8025bdb8(0x4f4d4248);  // 'OMBH'
    threshold = 10;
    if (DAT_805015bd == 1) {
        threshold = 0xf;
    } else if (DAT_805015bd == 0) {
        threshold = 7;
    } else if (DAT_805015bd < 3) {
        threshold = 0x19;
    }
    if (threshold <= count) {
        FUN_80221074((double)FLOAT_80617e90, param_2, param_3, param_4,
                     param_5, param_6, param_7, param_8);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 0x802110f4  ghz_level_master_tick
// Per-frame level master. Always runs the ambient sound trigger, the electric
// drop-bolt master, and the reactor arc-emitter. When interactive (mode flag
// 0x40 clear) it additionally runs the weather ramp -> bot weapon-swap events ->
// difficulty payload gate chain, then blocks scene portals 0xe / 0x14 / 0x18.
// ---------------------------------------------------------------------------
void ghz_level_master_tick(unsigned long long param_1, unsigned long long param_2,
                           unsigned long long param_3, unsigned long long param_4,
                           unsigned long long param_5, unsigned long long param_6,
                           unsigned long long param_7, unsigned long long param_8)
{
    unsigned long long chain;

    ghz_ambient_sound_trigger();
    ghz_bolt_master_tick();
    ghz_reactor_arc_emitter_tick();

    if ((DAT_805015c0 & 0x40) == 0) {
        // ROM chains r3 through weather ramp -> bot events -> difficulty gate.
        ghz_weather_ramp_tick();
        chain = (unsigned long long)(unsigned int)ghz_bot_weapon_swap_events(
                    param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
        ghz_difficulty_payload_gate(chain, (double)param_2, (double)param_3, (double)param_4,
                                    (double)param_5, (double)param_6, (double)param_7,
                                    (double)param_8);
        scene_portal_block(0xe);
        scene_portal_block(0x14);
        scene_portal_block(0x18);
    }
}

// ---------------------------------------------------------------------------
// 0x80211160  ghz_level_reset
// Level reset/re-arm: clear the ambient sound handle, rebuild the particle set
// (ghz_level_particle_setup), and interactive-only: reset the effect timer,
// register the MTHS HUD event dispatcher, reset the drop-trail rig, and set the
// occlusion/render flag bits on a fixed list of soundpad zones.
// ---------------------------------------------------------------------------
void ghz_level_reset(void)
{
    int zone;

    DAT_80610e4c = -1;
    ghz_level_particle_setup();

    if ((DAT_805015c0 & 0x40) == 0) {
        ghz_effect_progress_reset();
        FUN_802217d4((int*)&ghz_mths_event_dispatch);
        ghz_droptrail_reset();

        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x4dc) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x20000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5d3) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x61d) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5d7) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x410) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x3ff) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x20000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5f2) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5f6) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5fd) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5f5) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
        zone = DAT_80611b8c + soundpad_find_zone_by_id(0x5fe) * 0x20;
        *(unsigned int*)(zone + 4) |= 0x10000;
    }
}

// ---------------------------------------------------------------------------
// 0x80211310  ghz_level_particle_setup
// Build the level's 5 particle def+emitter pairs: spark burst A (def
// DAT_805c6884), spark burst B (DAT_805c6950), spin-spark C (DAT_805c6a1c),
// ground trail D (DAT_805c6ae8), and missile trail E (DAT_805c6bb4). Also resets
// the bolt state and clears the two cached emitter handles. The packed colour /
// size-keyframe literals are passed as the raw ints the ROM stores.
// ---------------------------------------------------------------------------
void ghz_level_particle_setup(void)
{
    double zero;
    int scaleE[3];   // local_1c/18/14
    int scaleD[3];   // local_28/24/20

    scaleE[0] = DAT_803dfee0; scaleE[1] = DAT_803dfee4; scaleE[2] = DAT_803dfee8;
    scaleD[0] = DAT_803dfeec; scaleD[1] = DAT_803dfef0; scaleD[2] = DAT_803dfef4;

    ghz_bolt_state_reset();
    particle_handle_clear((unsigned int*)&DAT_80612808);
    particle_handle_clear((unsigned int*)&DAT_80612804);

    // def/emitter A (spark burst)
    particle_def_create((double)FLOAT_80617ea4, (double)FLOAT_80617ea0, (int)&DAT_805c6884, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e90,
                                    (double)FLOAT_80617edc, 0xffa00000, 0x100c0800, 0x20181000);
    particle_def_set_gravity((double)FLOAT_80617fcc, 0x19140d00);
    particle_def_set_size_start_end((double)FLOAT_80617ed8, (double)FLOAT_80617f5c,
                                    (double)FLOAT_80617fcc, (double)FLOAT_80617fcc);
    particle_def_set_color_end((double)FLOAT_80618000, (double)FLOAT_80618004,
                               (double)FLOAT_80617fb8, (double)FLOAT_80617e90);
    particle_def_set_velocity_ranges((double)FLOAT_80617ea0, (double)FLOAT_80618008, (double)FLOAT_80617ef0,
                                     (double)FLOAT_80617fcc, (double)FLOAT_80617ea0, (double)FLOAT_80617f40);
    particle_def_set_drag((double)FLOAT_80617ee0);
    particle_emitter_create((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_8061800c,
                            (int)&DAT_805c65c4, (int)&DAT_805c6884, 0, 0x4000000);
    particle_emitter_set_color_range((double)FLOAT_80617e9c, (double)FLOAT_80617e90,
                                     (double)FLOAT_80618010, (double)FLOAT_80617e90);
    particle_emitter_set_lifetime_range((double)FLOAT_80617e9c, (double)FLOAT_80617e90);

    // def/emitter B (spark burst B)
    particle_def_create((double)FLOAT_80617e8c, (double)FLOAT_80617ea0, (int)&DAT_805c6950, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e90,
                                    (double)FLOAT_80617edc, 0x7f500000, 0x100c0800, 0x20181000);
    particle_def_set_gravity((double)FLOAT_80617ff4, 0x19140d00);
    particle_def_set_size_start_end((double)FLOAT_80617ed8, (double)FLOAT_80617f5c,
                                    (double)FLOAT_80617fcc, (double)FLOAT_80617fcc);
    particle_def_set_color_end((double)FLOAT_80618000, (double)FLOAT_80618004,
                               (double)FLOAT_80617fb8, (double)FLOAT_80617e90);
    particle_def_set_velocity_ranges((double)FLOAT_80618014, (double)FLOAT_80618008, (double)FLOAT_80617f40,
                                     (double)FLOAT_80617fcc, (double)FLOAT_80617ea0, (double)FLOAT_80617f40);
    particle_def_set_drag((double)FLOAT_80617f9c);
    particle_emitter_create((double)FLOAT_80618018, (double)FLOAT_80617e8c, (double)FLOAT_8061800c,
                            (int)&DAT_805c6800, (int)&DAT_805c6950, 0, 0x20000001);
    particle_emitter_set_color_range((double)FLOAT_80617ed0, (double)FLOAT_80617e90,
                                     (double)FLOAT_80617f2c, (double)FLOAT_80617e90);
    particle_emitter_set_lifetime_range((double)FLOAT_80617ed0, (double)FLOAT_80617e90);

    // def/emitter C (spin-spark)
    particle_def_create((double)FLOAT_80617e8c, (double)FLOAT_80617ea0, (int)&DAT_805c6a1c, 2, 1, 1, 0x32e, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e90,
                                    (double)FLOAT_80617edc, 0xf0f6f00, 0x6f00, 0x1000);
    particle_def_set_color_end((double)FLOAT_80618000, (double)FLOAT_80618004,
                               (double)FLOAT_80617f58, (double)FLOAT_80617e90);
    particle_def_set_spin_range((double)FLOAT_80617ef4, (double)FLOAT_80617ef4);
    particle_emitter_create((double)FLOAT_80618018, (double)FLOAT_80617e8c, (double)FLOAT_80617fcc,
                            (int)&DAT_805c66f8, (int)&DAT_805c6a1c, 0, 0x20000000);
    particle_emitter_set_color_range((double)FLOAT_80617ed0, (double)FLOAT_80617e90,
                                     (double)FLOAT_80617ea8, (double)FLOAT_80617e90);
    particle_emitter_set_lifetime_range((double)FLOAT_80617ed0, (double)FLOAT_80617e90);

    // def/emitter D (ground trail, scaled)
    particle_def_create((double)FLOAT_80617fbc, (double)FLOAT_80617ea0, (int)&DAT_805c6ae8, 0, 0, 1, 0x326, 0);
    particle_def_set_size_keyframes((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e90,
                                    (double)FLOAT_80618014, 0x40302000, 0x100c0800, 0x20181000);
    particle_def_set_size_start_end((double)FLOAT_80617ed8, (double)FLOAT_80617f5c,
                                    (double)FLOAT_80617fcc, (double)FLOAT_80617fcc);
    particle_def_set_color_end((double)FLOAT_8061801c, (double)FLOAT_80618004,
                               (double)FLOAT_80617e9c, (double)FLOAT_80617e90);
    particle_def_set_velocity_ranges((double)FLOAT_80617ea4, (double)FLOAT_80618008, (double)FLOAT_80617f40,
                                     (double)FLOAT_80617fcc, (double)FLOAT_80617ea0, (double)FLOAT_80617f40);
    particle_def_set_drag((double)FLOAT_80618020);
    particle_emitter_create((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e8c,
                            (int)&DAT_805c6674, (int)&DAT_805c6ae8, 3, 1);
    zero = (double)FLOAT_80617e90;
    particle_emitter_set_color_range(zero, zero, (double)FLOAT_80617efc, zero);
    particle_emitter_set_scale((int)scaleE);

    // def/emitter E (missile trail, scaled)
    particle_def_create((double)FLOAT_80617f40, (double)FLOAT_80617ea0, (int)&DAT_805c6bb4, 0, 0, 1, 0x300, 0);
    particle_def_set_size_keyframes((double)FLOAT_80618024, (double)FLOAT_80617e90, (double)FLOAT_80617e90,
                                    (double)FLOAT_80617e8c, 0x7f643c10, 0x7f643c10, 0);
    particle_def_set_size_start_end((double)FLOAT_80617ed8, (double)FLOAT_80617f5c,
                                    (double)FLOAT_80617fcc, (double)FLOAT_80617fcc);
    particle_def_set_color_end((double)FLOAT_8061801c, (double)FLOAT_80618004,
                               (double)FLOAT_80617e90, (double)FLOAT_80617e90);
    particle_def_set_velocity_ranges((double)FLOAT_80617efc, (double)FLOAT_80618028, (double)FLOAT_80617f2c,
                                     (double)FLOAT_80617ea0, (double)FLOAT_80617ea0, (double)FLOAT_80617f2c);
    particle_def_set_drag((double)FLOAT_80618020);
    particle_emitter_create((double)FLOAT_80617ea0, (double)FLOAT_80617e90, (double)FLOAT_80617e8c,
                            (int)&DAT_805c677c, (int)&DAT_805c6bb4, 3, 1);
    particle_emitter_set_color_range((double)FLOAT_80617f40, (double)FLOAT_80617e90,
                                     (double)FLOAT_8061802c, (double)FLOAT_80617e90);
    particle_emitter_set_scale((int)scaleD);
}

// ===========================================================================
// batch_657 : GHZ level environmental hazards
// 0x802117b0 - 0x8021258c  (10 functions, subsystem:game, worm-effect)
//
// Three self-contained hazard sub-systems layered on the GHZ reactor level:
//   * a random ambient-SFX scheduler backed by a 0x10-byte control block
//     (DAT_80612810): alloc + init + per-frame tick that plays a non-repeating
//     random sample from the 0x1ab0..0x1abb bank on a randomised interval.
//   * a rising/falling energy-beam prop ('MART'/'TRA2' actors): the object-init
//     picks a direction + start height and plays a startup 3D loop, and the
//     per-frame tick integrates the beam's Z position, kills characters caught in
//     its AABB (character_on_death_init), and pulses its vertex-colour brightness.
//   * a scripted auto-turret barrage (control DAT_805c6c80): setup latches the
//     enable flag, and the per-frame driver periodically picks the nearest of 5
//     fixed emitter points and lobs a projectile at the player with rng spread.
//
// Non-matching: 4CC ids ('MART' 0x5452414d, 'TRA2' 0x54524132); 0x43300000
// int<->double bias math written as plain float math; the (uint^0x80000000)/2^32
// signed-rng->float pattern written directly; the sine LUT (DAT_80583228,
// &0x7ff quarter-phase, +0x200 cos offset) indexed directly. Several ROM helpers
// whose generated_decls prototype drops the r3 return / differs from the call-site
// ABI are routed through file-local typed aliases.
// ---------------------------------------------------------------------------

// --- batch_657 raw data symbols (file-local) ---
extern unsigned int* DAT_80612810;   // ambient-SFX control block ptr ([0]=handle, [1]=last id, [2]=timer float)
extern int   DAT_806108e4;      // gameplay-active gate
extern int   DAT_80612098;      // spark-source list end index
extern int   DAT_80588610;      // spark-source pointer array base
extern int   DAT_805c6c80;      // turret enable flag (latched from trigger)
extern float DAT_805c6c84;      // turret fire timer
extern float DAT_805c6c88;      // turret distance-charge accumulator
extern int   DAT_80459ff8;      // turret emitter table: point[i].x  (0x10 stride, 5 rows)
extern int   DAT_80459ffc;      // turret emitter table: point[i].y
extern int   DAT_8045a000;      // turret emitter table: point[i].z
extern int   DAT_8045a00c;      // turret emitter table: point[i].ownerId (base -0x7fba600c)

// batch_657 tuning float pool 0x80618030..0x806180e0 (+ bias doubles)
extern float  FLOAT_80618030; extern float FLOAT_80618034; extern float FLOAT_80618038;
extern float  FLOAT_8061803c; extern float FLOAT_80618040; extern float FLOAT_80618044;
extern double DOUBLE_80618048;
extern float  FLOAT_80618050; extern float FLOAT_80618054; extern float FLOAT_80618058;
extern float  FLOAT_8061805c; extern float FLOAT_80618060; extern float FLOAT_80618064;
extern float  FLOAT_80618068; extern float FLOAT_8061806c; extern float FLOAT_80618070;
extern float  FLOAT_80618074; extern float FLOAT_80618078; extern float FLOAT_8061807c;
extern float  FLOAT_80618080; extern float FLOAT_80618084; extern double DOUBLE_80618088;
extern float  FLOAT_80618090; extern float FLOAT_80618094; extern float FLOAT_80618098;
extern float  FLOAT_8061809c; extern float FLOAT_806180a0; extern float FLOAT_806180a4;
extern float  FLOAT_806180a8; extern float FLOAT_806180ac; extern float FLOAT_806180b0;
extern float  FLOAT_806180b4; extern float FLOAT_806180b8; extern float FLOAT_806180bc;
extern float  FLOAT_806180c0; extern float FLOAT_806180c4; extern float FLOAT_806180c8;
extern double DOUBLE_806180d0; extern double DOUBLE_806180d8; extern float FLOAT_806180e0;

// sine LUT (shared with worm_ghz_state.cpp) - 0x800-entry, &0x7ff quarter-phase.
extern int    DAT_80583228;

// --- batch_657 helpers reached by raw address (file-local externs) ---
extern int  FUN_803826e8(void);        // FP spill frame open (view/entity getter)
extern void FUN_80382734(void);        // FP spill frame close
extern int  FUN_8023c32c(unsigned int classWord); // resolve linked actor by 4CC

// mem_alloc is (s32,s32)->s32 in generated_decls; the ROM call site here reduces
// to (0x10, 1).  game_frand / game_rand / sound_resolve_sample_id / sound_is_playing
// / entity_get_world_position / sound_get_position / arc_noise_nop / math_sqrt /
// SQRT resolve through the generated headers.

// character_on_death_init is declared (int) in generated_decls but the ROM call
// threads the world position via the FP-ABI (doubles) ahead of the entity ptr.
typedef void (*GhzCharDeathFn)(double z, double y2, double y, double x,
                               double refY, unsigned long long f6, unsigned long long f7,
                               unsigned long long f8, int entity);
static const GhzCharDeathFn __ghz_char_on_death = (GhzCharDeathFn)&character_on_death_init;

// entity_apply_vertex_color_offset is declared with 6 params, but the ROM call
// passes (entity, index, r,g,b, 0,0,0) = 8.  Route through an 8-arg alias.
typedef void (*GhzVtxColorFn)(int entity, int index, char r, char g, char b,
                              int rOv, int gOv, unsigned char bOv);
static const GhzVtxColorFn __ghz_vtx_color_offset = (GhzVtxColorFn)&entity_apply_vertex_color_offset;

// player_ai_projectile_spawn_simple: ROM shape used by the turret barrage.
typedef void (*GhzProjSimpleFn)(double accuracy, double f2, int projType, float* pos, int* dir,
                                int owner, int* soundDef, int weaponMode, int a, int ownerId,
                                int b, int c);
static const GhzProjSimpleFn __ghz_proj_simple = (GhzProjSimpleFn)&player_ai_projectile_spawn_simple;

// mem_alloc typed to the reduced (size,type)->handle shape the call site uses.
typedef unsigned int* (*GhzMemAllocFn)(int size, int type);
static const GhzMemAllocFn __ghz_mem_alloc = (GhzMemAllocFn)&mem_alloc;

// game_rand is a permissive variadic extern in generated_externs; bind a no-arg
// int-returning alias for the call sites here.
typedef unsigned int (*GhzGameRandFn)(void);
static const GhzGameRandFn __ghz_game_rand = (GhzGameRandFn)&game_rand;

// forward decls (registered by address / mutually referenced).
void ghz_beam_prop_init(void);
void ghz_beam_prop_tick(void);

// ---------------------------------------------------------------------------
// 0x802117b0  ghz_ambient_sfx_alloc
// Allocate the 0x10-byte ambient-SFX control block into DAT_80612810.
// ---------------------------------------------------------------------------
void ghz_ambient_sfx_alloc(void)
{
    DAT_80612810 = __ghz_mem_alloc(0x10, 1);
}

// ---------------------------------------------------------------------------
// 0x802117dc  ghz_ambient_sfx_init
// Clear the control block, set the sound handle (+0) to -1 (none playing), and
// seed the interval timer (+2) with a random value in [FLOAT_80618030, +34].
// ---------------------------------------------------------------------------
void ghz_ambient_sfx_init(void)
{
    double r;
    memset_wrapper((int)DAT_80612810, 0, 0xc);
    DAT_80612810[0] = 0xffffffff;
    r = game_frand();
    *(float*)&DAT_80612810[2] = (float)((double)FLOAT_80618034 * r + (double)FLOAT_80618030);
}

// ---------------------------------------------------------------------------
// 0x8021182c  ghz_ambient_sfx_tick
// Per-frame ambient-SFX scheduler. While gameplay is active: if no sound is
// playing (+0 < 0) count down the interval timer (+2) at a frame-scaled rate; on
// expiry pick a random non-repeating sample id in the 0x1ab0..0x1abb bank, play
// it, latch the handle, and re-seed the timer. If a sound is playing, detect
// completion and reset the handle to -1.
// ---------------------------------------------------------------------------
void ghz_ambient_sfx_tick(void)
{
    unsigned int lastId;
    unsigned int pick;
    int sampleId;
    double r;
    int params[14];

    if (DAT_806108e4 == 0) {
        return;
    }

    if ((int)DAT_80612810[0] < 0) {
        // interval countdown (frame-scaled bias math preserved as float)
        *(float*)&DAT_80612810[2] =
            -(FLOAT_806108ec *
              (FLOAT_80618038 /
               (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618048)) -
              *(float*)&DAT_80612810[2]);

        if (*(float*)&DAT_80612810[2] <= FLOAT_8061803c) {
            lastId = DAT_80612810[1];
            if ((int)lastId < 0x1ab0 || 0x1abb < (int)lastId) {
                pick = __ghz_game_rand() % 0xc + 0x1ab0;
            } else {
                pick = __ghz_game_rand() % 0xb + 0x1ab0;
                // skip the previous id (non-repeat) with a sign-corrected bump
                pick = pick + ((unsigned int)(lastId <= pick) - ((int)lastId >> 0x1f));
            }
            DAT_80612810[1] = pick;
            sampleId = sound_resolve_sample_id(pick);
            sound_init_play_params((int)params);
            params[0] = sampleId;
            DAT_80612810[0] = sound_play_request(params);
            r = game_frand();
            *(float*)&DAT_80612810[2] = (float)((double)FLOAT_80618044 * r + (double)FLOAT_80618040);
        }
    } else {
        if (sound_is_playing(DAT_80612810[0]) == 0) {
            DAT_80612810[0] = 0xffffffff;
        }
    }
}

// ---------------------------------------------------------------------------
// 0x80211978  ghz_beam_prop_tick
// Per-frame render/update for the rising/falling energy-beam prop. Integrates the
// beam Z position (+0x6c) at a frame-scaled rate by its velocity (+0xbc), tracks
// the 3D sound position, transitions the state (+0x1a0) when it passes the top/
// bottom threshold, performs an AABB overlap test against every spark source
// (DAT_80588610 list, +0x24 bit 8 gate) killing any overlapping character
// (character_on_death_init), then applies a height-driven vertex-colour
// brightness offset. FP-spill FUN_803826e8/80382734.
// ---------------------------------------------------------------------------
void ghz_beam_prop_tick(void)
{
    int entity;
    int state;
    int srcOff;
    int i;
    int colorOff;
    char cc;
    float z;
    float* pos;
    float lo0, lo1, lo2, hi0, hi1, hi2;
    double in_f6 = 0, in_f7 = 0, in_f8 = 0;

    entity = FUN_803826e8();

    // integrate Z by velocity (+0xbc), frame-scaled
    *(float*)(entity + 0x6c) = *(float*)(entity + 0x6c) +
        (FLOAT_80618050 *
         (float)((double)(int)(unsigned int)(DAT_806108e4 ^ 0x80000000) - DOUBLE_80618048) *
         *(float*)(entity + 0xbc)) /
        (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_80618048);

    pos = entity_get_world_position(entity);
    sound_get_position(*(int*)(entity + 0x22c), pos);
    sound_is_playing(*(unsigned int*)(entity + 0x22c));
    sound_is_playing(*(unsigned int*)(entity + 0x22c));
    entity_get_world_position(entity);
    arc_noise_nop();

    state = *(int*)(entity + 0x1a0);
    if (state == 2) {
        if (*(float*)(entity + 0x6c) < FLOAT_80618058) {
            entity_register_for_update(entity, 3, 0);
            *(int*)(entity + 0x1a0) = 0;
        }
    } else if (state < 2 && 0 < state && FLOAT_8061805c < *(float*)(entity + 0x6c)) {
        entity_register_for_update(entity, 3, 0);
        *(int*)(entity + 0x1a0) = 0;
    }

    // AABB (base +/- extent) around the beam, in world space
    {
        float baseX = *(float*)(entity + 0x64);
        float baseY = *(float*)(entity + 0x68);
        float baseZ = *(float*)(entity + 0x6c);
        float eX = *(float*)(entity + 0x1a8), cX = *(float*)(entity + 0x1c0);
        float eY = *(float*)(entity + 0x1ac), cY = *(float*)(entity + 0x1c4);
        float eZ = *(float*)(entity + 0x1b0), cZ = *(float*)(entity + 0x1c8);
        lo0 = (eX - cX) + baseX; hi0 = (eX + cX) + baseX;
        lo1 = (eY - cY) + baseY; hi1 = (eY + cY) + baseY;
        lo2 = (eZ - cZ) + baseZ; hi2 = (eZ + cZ) + baseZ;
    }

    srcOff = 0;
    for (i = 0; i < DAT_80612098; i = i + 1) {
        int* srcRec = *(int**)((int)&DAT_80588610 + srcOff);
        int tgt = *srcRec;
        if ((srcRec[9] & 8U) == 0) {
            float tx = *(float*)(tgt + 0x64) + *(float*)(tgt + 0x1a8);
            float ty = *(float*)(tgt + 0x68) + *(float*)(tgt + 0x1ac);
            float tz = *(float*)(tgt + 0x6c) + *(float*)(tgt + 0x1b0);
            if (lo0 <= tx && tx <= hi0 && lo1 <= ty && ty <= hi1 && lo2 <= tz && tz <= hi2) {
                __ghz_char_on_death((double)*(float*)(tgt + 0x6c), (double)ty,
                                    (double)*(float*)(tgt + 0x68), (double)tx,
                                    (double)*(float*)(entity + 0x68), in_f6, in_f7, in_f8, tgt);
            }
        }
        srcOff = srcOff + 4;
    }

    // height-driven vertex-colour brightness offset
    z = *(float*)(entity + 0x6c);
    colorOff = 0;
    if (FLOAT_80618060 <= z) {
        if (FLOAT_80618070 < z) {
            colorOff = (int)((FLOAT_80618064 * (FLOAT_8061805c - z)) / FLOAT_8061806c) - 0x7f;
        }
    } else {
        colorOff = (int)((FLOAT_80618064 * (z - FLOAT_80618068)) / FLOAT_8061806c) - 0x7f;
    }
    if (colorOff != 0) {
        cc = (char)colorOff;
        __ghz_vtx_color_offset(entity, 0, cc, cc, cc, 0, 0, 0);
        __ghz_vtx_color_offset(entity, 1, cc, cc, cc, 0, 0, 0);
    }
    FUN_80382734();
}

// ---------------------------------------------------------------------------
// 0x80211cf0  ghz_beam_prop_init
// Object-init/activation for the energy-beam prop (state +0x1a0 == 0). Seeds a
// random Z velocity (+0xbc), plays a startup 3D loop (bank 0x624 + random pitch/
// params), registers the per-frame tick (ghz_beam_prop_tick), then randomly picks
// a start position: falling from the top (state 2, negated velocity, start
// FLOAT_8061805c) or rising from the bottom (state 1, start FLOAT_80618058).
// ---------------------------------------------------------------------------
void ghz_beam_prop_init(void)
{
    int entity;
    unsigned int r0, r1, r2;
    int pitchId;
    float* pos;
    int params[4];
    unsigned int soundFlags;
    float paramVol;
    float paramPitch;
    int pos0, pos1, pos2;

    entity = FUN_803826f0();
    if (entity != 0 && *(int*)(entity + 0x1a0) == 0) {
        r0 = __ghz_rng();
        *(float*)(entity + 0xbc) =
            FLOAT_80618078 *
            ((float)((double)(int)(unsigned int)r0 - DOUBLE_80618088) / FLOAT_8061807c) +
            FLOAT_80618074;
        pos = entity_get_world_position(entity);
        paramPitch = *(float*)(entity + 0xbc);

        r1 = __ghz_rng();
        pitchId = (int)(FLOAT_80618080 +
                        (float)((double)(int)(unsigned int)r1 - DOUBLE_80618088) / FLOAT_8061807c);

        sound_init_play_params((int)params);
        // ROM sets a flag block + params: paramVol/pitch + position + sample id.
        soundFlags = 0xa0;
        (void)soundFlags;
        paramVol = FLOAT_80618084;
        pos0 = *(int*)&pos[0]; pos1 = *(int*)&pos[1]; pos2 = *(int*)&pos[2];
        (void)paramVol; (void)pos0; (void)pos1; (void)pos2;
        params[0] = pitchId + 0x624;
        *(unsigned int*)(entity + 0x22c) = sound_play_request(params);

        entity_register_for_update(entity, 3, (int)(intptr_t)&ghz_beam_prop_tick);

        r2 = __ghz_rng();
        if ((float)((double)(int)(unsigned int)r2 - DOUBLE_80618088) / FLOAT_8061807c <= FLOAT_80618080) {
            *(float*)(entity + 0x6c) = FLOAT_8061805c;
            *(float*)(entity + 0xbc) = -*(float*)(entity + 0xbc);
            *(int*)(entity + 0x1a0) = 2;
        } else {
            *(float*)(entity + 0x6c) = FLOAT_80618058;
            *(int*)(entity + 0x1a0) = 1;
        }
    }
    FUN_8038273c();
}

// ---------------------------------------------------------------------------
// 0x80211e8c  ghz_beam_prop_dispatch
// Resolve the two beam-prop actors 'MART' (0x5452414d) and 'TRA2' (0x54524132)
// and run the beam init on each.
// ---------------------------------------------------------------------------
void ghz_beam_prop_dispatch(void)
{
    FUN_8023c32c(0x5452414d);  // 'MART'
    ghz_beam_prop_init();
    FUN_8023c32c(0x54524132);  // 'TRA2'
    ghz_beam_prop_init();
}

// ---------------------------------------------------------------------------
// 0x80211ec8  ghz_beam_prop_object_init
// Object-init switch for the beam-prop class ids (0x97..0x9c): classes
// 0x97/0x98/0x9c become damage-receivers (render cb damage_entity_receive) with
// health fields (+0x1cc/+0x1d8) and a regen field (+0x1dc); class 0x9b clears the
// beam state (+0x1a0 = 0).
// ---------------------------------------------------------------------------
void ghz_beam_prop_object_init(int entity)
{
    unsigned short cls = *(unsigned short*)(entity + 0x94);

    if (cls == 0x9a) {
        return;
    }
    if (cls < 0x9a) {
        if (cls == 0x98 || (0x97 < cls)) {
            *(int*)(*(int*)(entity + 4) + 0x30) = 3;
            entity_set_render_type(entity, (int)(intptr_t)&damage_entity_receive);
            *(float*)(entity + 0x1d8) = FLOAT_80618054;
            *(float*)(entity + 0x1cc) = FLOAT_80618054;
            *(float*)(entity + 0x1dc) = FLOAT_80618080;
        }
    } else if (cls == 0x9c) {
        *(int*)(*(int*)(entity + 4) + 0x30) = 3;
        entity_set_render_type(entity, (int)(intptr_t)&damage_entity_receive);
        *(float*)(entity + 0x1d8) = FLOAT_80618054;
        *(float*)(entity + 0x1cc) = FLOAT_80618054;
        *(float*)(entity + 0x1dc) = FLOAT_80618080;
    } else if (cls < 0x9c) {
        *(int*)(entity + 0x1a0) = 0;
    }
}

// ---------------------------------------------------------------------------
// 0x80211fb4  ghz_turret_barrage_tick
// Per-frame scripted auto-turret barrage (enabled by DAT_805c6c80). Tracks the
// player's velocity/distance to ramp two accumulators: a fire timer
// (DAT_805c6c84) and a distance-charge (DAT_805c6c88). When the timer fires: pick
// the nearest of 5 fixed emitter points (DAT_80459ff8 table, 0x10 stride),
// compute an rng-spread aim direction via the sine LUT, play sound 0xc47, spawn a
// type-9 projectile from that emitter toward the player, and re-seed the timer.
// ---------------------------------------------------------------------------
void ghz_turret_barrage_tick(void)
{
    unsigned int r;
    int nearest;
    int i;
    int projOwner;
    float spread;
    unsigned int idxU, idxV;
    float aim[3];
    float firePt[3];
    int params[6];
    float paramVol, paramPitch;
    double dist, bestSq, sq;

    if (DAT_806108e4 == 0 || DAT_805c6c80 == 0) {
        return;
    }

    float dx = *(float*)(DAT_80611d74 + 0xa8) - *(float*)(DAT_80611d74 + 0x14c);
    float dy = *(float*)(DAT_80611d74 + 0xac) - *(float*)(DAT_80611d74 + 0x150);
    float dz = *(float*)(DAT_80611d74 + 0xb0) - *(float*)(DAT_80611d74 + 0x154);

    // fire-timer countdown (frame-scaled)
    DAT_805c6c84 = -((FLOAT_80618090 /
        (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0)) *
        FLOAT_806108ec - DAT_805c6c84);

    dist = math_sqrt((double)(dz * dz + dx * dx + dy * dy));
    // distance-charge accumulate + decay
    {
        float rate = (FLOAT_80618090 /
            (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0)) *
            FLOAT_806108ec;
        float acc = DAT_805c6c88 + (float)(dist / (double)rate);
        DAT_805c6c88 = acc - (FLOAT_806108ec * acc) / FLOAT_80618094;
    }

    if (DAT_805c6c84 < FLOAT_80618098) {
        nearest = -1;
        bestSq = (double)FLOAT_8061809c;

        r = __ghz_rng();
        double dv = (double)(FLOAT_806180a0 *
            ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4));
        r = __ghz_rng();
        spread = FLOAT_806180a8 * DAT_805c6c88 *
                 (FLOAT_806180b0 *
                  ((float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4) +
                  FLOAT_806180ac);
        idxU = (unsigned int)((double)FLOAT_806180b4 * dv + (double)FLOAT_806180b0);
        idxV = idxU;

        for (i = 0; i < 5; i = i + 1) {
            float px = (float)(&DAT_80459ff8)[i * 4];
            float py = (float)(&DAT_80459ffc)[i * 4];
            float pz = (float)(&DAT_8045a000)[i * 4];
            float ex = *(float*)(DAT_80611d74 + 0xa8) - px;
            float ey = *(float*)(DAT_80611d74 + 0xac) - py;
            float ez = *(float*)(DAT_80611d74 + 0xb0) - pz;
            sq = (double)(ez * ez + ex * ex + ey * ey);
            if (sq < bestSq) { nearest = i; bestSq = sq; }
        }

        // rng-spread aim direction from nearest emitter toward player
        aim[0] = ((float)(&DAT_80583228)[(idxU + 0x200U) & 0x7ff] * spread +
                  *(float*)(DAT_80611d74 + 0xa8)) - (float)(&DAT_80459ff8)[nearest * 4];
        aim[1] = (FLOAT_80618098 * spread + *(float*)(DAT_80611d74 + 0xac)) -
                 (float)(&DAT_80459ffc)[nearest * 4];
        aim[2] = ((float)(&DAT_80583228)[idxV & 0x7ff] * spread + *(float*)(DAT_80611d74 + 0xb0)) -
                 (float)(&DAT_8045a000)[nearest * 4];
        {
            float m2 = aim[2] * aim[2] + aim[0] * aim[0] + aim[1] * aim[1];
            if (FLOAT_806180b8 < m2) {
                float inv = 1.0f / SQRT(m2);
                inv = inv * -(FLOAT_806180b0 * m2 * inv * inv - FLOAT_806180bc);
                float s = FLOAT_80618090 * inv * -(FLOAT_806180b0 * m2 * inv * inv - FLOAT_806180bc);
                aim[0] = aim[0] * s; aim[1] = aim[1] * s; aim[2] = aim[2] * s;
            }
        }

        firePt[0] = FLOAT_806180b0 * aim[0] + (float)(&DAT_80459ff8)[nearest * 4];
        firePt[1] = FLOAT_806180b0 * aim[1] + (float)(&DAT_80459ffc)[nearest * 4];
        firePt[2] = FLOAT_806180b0 * aim[2] + (float)(&DAT_8045a000)[nearest * 4];

        sound_init_play_params((int)params);
        params[0] = 0xc47;
        paramVol = FLOAT_806180c0;
        paramPitch = FLOAT_806180bc;
        *(float*)&params[3] = paramPitch;   // local_70 pitch (+0xc)
        *(float*)&params[4] = firePt[0];    // position packed into the params tail
        (void)paramVol;
        sound_play_request(params);

        projOwner = (&DAT_8045a00c)[nearest * 4];   // emitter owner id (base -0x7fba600c)
        __ghz_proj_simple((double)FLOAT_806180c4, (double)FLOAT_80618098, 9, firePt, (int*)aim, 0,
                          (int*)0, 0, 0, projOwner, 0, 2);

        r = __ghz_rng();
        DAT_805c6c84 = FLOAT_806180c8 +
            (float)((double)(int)(unsigned int)r - DOUBLE_806180d8) / FLOAT_806180a4;
    }
}

// ---------------------------------------------------------------------------
// 0x8021258c  ghz_turret_barrage_setup
// Latch the turret enable flag from the trigger entity (*rec + 0xc) and seed the
// distance-charge accumulator.
// ---------------------------------------------------------------------------
void ghz_turret_barrage_setup(int* rec)
{
    DAT_805c6c80 = *(int*)(*rec + 0xc);
    DAT_805c6c88 = FLOAT_806180e0 *
        (float)((double)(int)(unsigned int)(DAT_806109c0 ^ 0x80000000) - DOUBLE_806180d0);
}

// ---------------------------------------------------------------------------
// 0x802125e0  ghz_r110_weapon_swap_event
// Scripted anim-event handler for the 'R110' (0x52313130) bot: on anim clip
// 0x7cb, swap the bot's weapon to slot 0x19, play the swap sound via the local
// player's sound block, and re-apply the slot selection. (Sibling of
// ghz_bot_weapon_swap_events for a single bot/clip.)
// ---------------------------------------------------------------------------
void ghz_r110_weapon_swap_event(unsigned long long param_1, unsigned long long param_2,
                                unsigned long long param_3, unsigned long long param_4,
                                unsigned long long param_5, unsigned long long param_6,
                                unsigned long long param_7, unsigned long long param_8)
{
    int bot;
    int localSnd;
    (void)param_1;

    bot = aiactor_find_bot_by_linked_entity(0x52313130);  // 'R110'
    if (bot != 0 && __ghz_anim_event_scan() != 0 &&
        *(int*)anim_active_clip_slot(bot) == 0x7cb) {
        int botObj = *(int*)(bot + 0x7c);
        localSnd = *(int*)(*(int*)(DAT_80611d74 + 0x14) + 0x7c);
        object_sound_slot_add_scaled(localSnd, 0x19, 8);
        object_slot_switch_notify((unsigned long long)(unsigned int)localSnd, param_2, param_3,
                                  param_4, param_5, param_6, param_7, param_8);
        object_set_weapon_slot(botObj, 0x19);
        object_slot_deselect();
        object_slot_select_apply();
    }
}

} // extern "C"
