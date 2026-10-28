// worm_mths_level.cpp
// "MTHS" level driver — a GHZ-reactor-derived set-piece level. Reuses the GHZ
// reactor tick machinery (ambient sound / bolt / arc emitter / weather ramp) and
// adds its own level setup (two actor tables, ~13 soundpad reverb zones, an event
// dispatcher ghz_mths_event_dispatch), a screen-effect object event handler, a
// soundpad-zone tracker (DAT_80612850), preload, portal closers, and a scripted
// bot weapon-swap sequence keyed off animation events.
//
// Batch: batch_664  (addresses 0x8021a56c .. 0x8021ab0c)

#include "common.h"

// ---------------------------------------------------------------------------
// Named cross-module helpers. generated_decls.h is not pulled into this TU, so
// (matching the worm_rain_level.cpp / worm_ghz_* convention) every helper is
// declared as a file-local extern with a signature matching its call site.
// ---------------------------------------------------------------------------
extern unsigned int aiactor_find_bot_by_linked_entity(int linkedId);
extern int  anim_event_scan_active(void);
extern int *anim_active_clip_slot(int bot);
extern void object_set_weapon_slot(int object, int slot);
extern void object_slot_deselect(void);
extern void object_sound_slot_accumulate(int object, int soundId, int amount);
extern void entity_set_render_type(int entity, int renderType);
extern int  screen_effect_alloc_slot(void);
extern void screen_effect_init_slot(int slot, int entity);
extern int  soundpad_find_zone_by_id(int zoneId);
extern int *soundpad_find_any_portal(int *zoneA, int *zoneB);
extern void level_resource_pool_setup(long long, double, double, long long,
                                      long long, long long, long long, long long);
extern long long level_actors_register(long long, double, double, long long,
                                        long long, long long, long long,
                                        long long, int slot, void *table, int n);

// GHZ reactor helpers reused by this level (defined in the worm_ghz_* files).
extern long long ghz_level_preload(long long, double, double, long long, long long,
                                   long long, long long, long long);
extern void ghz_ambient_sound_trigger(void);
extern void ghz_bolt_master_tick(void);
extern void ghz_reactor_arc_emitter_thunk(void);
extern void ghz_weather_ramp_tick(void);
extern void ghz_level_particle_setup(void);
extern void ghz_effect_progress_reset(void);
extern void ghz_mths_event_dispatch(void);
extern void damage_filter_arcade_match_end(void);

// entity_preload_resources' shared decl differs in arity from the wide ABI call
// here; bind by ROM address as a file-local typed alias.
typedef long long (*preload_res_fp)(long long, double, double, long long, long long,
                                    long long, long long, long long, int);
static const preload_res_fp __mths_preload_res = (preload_res_fp)0x80070ba8; // entity_preload_resources

// ---------------------------------------------------------------------------
// File-local externs for raw ROM symbols still un-named elsewhere in the tree.
// ---------------------------------------------------------------------------
extern void FUN_803826e4(void);   // scoped-entity push
extern void FUN_80382730(void);   // scoped-entity pop
extern int  FUN_8025b8c0(int a);  // arcade-match gate predicate (returns nonzero if active)
extern void FUN_802215a8(int a, int b, int c, int d, int e); // mths sequence config
extern void FUN_802217d4(int cb); // register mths event-dispatch callback

// Raw data / constants
extern int    DAT_80611b8c;   // soundpad zone array base
extern int    DAT_80611d74;   // HUD/screen-effect publish record base
extern int    DAT_80610e4c;   // mths sequence handle (reset to -1)
extern int    DAT_80612850;   // tracked soundpad zone index
extern float  FLOAT_80610e7c; // cached bot metric (from rainlvl_bot_link_event / batch_663)
extern int    DAT_8045b94c;   // actor table 0
extern int    DAT_8045b964;   // actor table 1
extern int    DAT_8045c0d8;   // portal-pair table A (id at +0, +4; 8-byte stride)
extern int    DAT_8045c0dc;
extern int    DAT_8045c0e8;   // portal-pair table B
extern int    DAT_8045c0ec;

// ---------------------------------------------------------------------------
// 0x8021a56c  mths_bot_weapon_swap_tick
// Scripted bot weapon-swap sequence driven by animation events. When the '1SBR'
// bot reaches anim clip 0xa97, it hands weapon slot 0x1b to its object and, once
// the event fires again, forces the 'ZCTP' bot's object to weapon 0x19 (playing
// sound 0x12). Separately, when the 'ZCTP' bot hits clip 0xa93 and isn't already
// on weapon 0x19, it swaps to 0x19 with the same sound.
// ---------------------------------------------------------------------------
void mths_bot_weapon_swap_tick(void)
{
    int bot;
    int other;
    int *clip;

    bot = (int)aiactor_find_bot_by_linked_entity(0x52425331);   // '1SBR'
    if (bot != 0) {
        if ((anim_event_scan_active() != 0) &&
            (clip = anim_active_clip_slot(bot), *clip == 0xa97)) {
            object_set_weapon_slot(*(int *)(bot + 0x7c), 0x1b);
            object_slot_deselect();
        }
        if (anim_event_scan_active() != 0) {
            clip = anim_active_clip_slot(bot);
            if (*clip == 0xa97) {
                other = (int)aiactor_find_bot_by_linked_entity(0x5054435a);  // 'ZCTP'
                other = *(int *)(other + 0x7c);
                bot = *(int *)(bot + 0x7c);
                *(int *)(other + 0x94) = 0x19;
                *(unsigned char *)(other + 0x54c) = 0;
                object_sound_slot_accumulate(other, 0x12, 1000);
                object_set_weapon_slot(bot, 0x19);
                object_slot_deselect();
            }
        }
    }

    bot = (int)aiactor_find_bot_by_linked_entity(0x5054435a);   // 'ZCTP'
    if (((bot != 0) && (anim_event_scan_active() != 0)) &&
        (clip = anim_active_clip_slot(bot), *clip == 0xa93) &&
        (bot = *(int *)(bot + 0x7c), *(int *)(bot + 0x94) != 0x19)) {
        *(int *)(bot + 0x94) = 0x19;
        *(unsigned char *)(bot + 0x54c) = 0;
        object_sound_slot_accumulate(bot, 0x12, 1000);
    }
}

// ---------------------------------------------------------------------------
// 0x8021a6c0  mths_screen_effect_object_event
// Object event handler for the MTHS screen-effect entity. Event 6 installs the
// screen effect: sets the render type to damage_filter_arcade_match_end, allocs
// and binds a screen-effect slot (ent+0x268), publishes the entity pointer to
// the HUD record (DAT_80611d74+0xe8c), and — if the arcade-match gate is active
// — seeds the effect intensity (ent+0x1cc) from FLOAT_80610e7c. Events 3/5 clear
// the published pointer. Event 1 with a matching owner (ctx+4) returns -1.
// ---------------------------------------------------------------------------
unsigned int mths_screen_effect_object_event(int ent, int event, int ctx)
{
    int slot;

    if (event == 4) {
        return 0;
    }
    if (event < 4) {
        if (event == 2) {
            return 0;
        }
        if (event < 2) {
            if (event < 1) {
                return 0;
            }
            if (*(int *)(ctx + 4) != ent) {
                return 0;
            }
            return 0xffffffff;
        }
        // event == 3 falls through to the clear-published-pointer path below.
    } else {
        if (event == 6) {
            entity_set_render_type(ent, (int)0x80091f3c);  // damage_filter_arcade_match_end
            slot = screen_effect_alloc_slot();
            *(int *)(ent + 0x268) = slot;
            screen_effect_init_slot(*(int *)(ent + 0x268), ent);
            *(int *)(DAT_80611d74 + 0xe8c) = ent;
            if (FUN_8025b8c0(0) == 0) {
                return 0xfffffffe;
            }
            *(float *)(ent + 0x1cc) = FLOAT_80610e7c;
            return 0xfffffffe;
        }
        if (5 < event) {
            return 0;
        }
        // event == 5 falls through to the clear path below.
    }
    *(int *)(DAT_80611d74 + 0xe8c) = 0;
    return 0xfffffffe;
}

// ---------------------------------------------------------------------------
// 0x8021a798  mths_soundzone_set_by_id  — track a soundpad zone by id.
// ---------------------------------------------------------------------------
void mths_soundzone_set_by_id(int zoneId)
{
    DAT_80612850 = soundpad_find_zone_by_id(zoneId);
}

// ---------------------------------------------------------------------------
// 0x8021a7bc  mths_soundzone_get  — return the tracked soundpad zone index.
// ---------------------------------------------------------------------------
int mths_soundzone_get(void)
{
    return DAT_80612850;
}

// ---------------------------------------------------------------------------
// 0x8021a7c4  mths_soundzone_set_default  — track the default zone (0x580).
// ---------------------------------------------------------------------------
void mths_soundzone_set_default(void)
{
    DAT_80612850 = soundpad_find_zone_by_id(0x580);
}

// ---------------------------------------------------------------------------
// 0x8021a7ec  mths_preload
// Preloads the GHZ level resources then the two MTHS-specific entity classes
// (0x53f and 0xe5).
// ---------------------------------------------------------------------------
void mths_preload(long long p1, double p2, double p3, long long p4, long long p5,
                  long long p6, long long p7, long long p8)
{
    long long chain;
    chain = ghz_level_preload(p1, p2, p3, p4, p5, p6, p7, p8);
    chain = __mths_preload_res(chain, p2, p3, p4, p5, p6, p7, p8, 0x53f);
    __mths_preload_res(chain, p2, p3, p4, p5, p6, p7, p8, 0xe5);
}

// ---------------------------------------------------------------------------
// 0x8021a81c  mths_master_tick
// Per-frame master driver: ticks the reused GHZ ambient sound, bolt master, arc
// emitter and weather ramp, plus the level's own bot weapon-swap sequence.
// ---------------------------------------------------------------------------
void mths_master_tick(void)
{
    ghz_ambient_sound_trigger();
    ghz_bolt_master_tick();
    ghz_reactor_arc_emitter_thunk();
    mths_bot_weapon_swap_tick();
    ghz_weather_ramp_tick();
}

// ---------------------------------------------------------------------------
// 0x8021a84c  mths_level_setup
// Level initialiser: registers two actor tables, sets up the resource pool,
// resets the sequence handle (DAT_80610e4c = -1), runs particle setup and effect
// progress reset, configures the MTHS sequence (FUN_802215a8) and registers its
// event dispatcher (ghz_mths_event_dispatch), flags ~13 soundpad zones as reverb
// (0x20000 large / 0x10000 small), and sets the default tracked soundzone.
// ---------------------------------------------------------------------------
void mths_level_setup(long long p1, double p2, double p3, long long p4, long long p5,
                      long long p6, long long p7, long long p8)
{
    long long chain;

    chain = level_actors_register(p1, p2, p3, p4, p5, p6, p7, p8, 0, &DAT_8045b94c, 1);
    chain = level_actors_register(chain, p2, p3, p4, p5, p6, p7, p8, 1, &DAT_8045b964, 1);
    level_resource_pool_setup(chain, p2, p3, p4, p5, p6, p7, p8);

    DAT_80610e4c = -1;
    ghz_level_particle_setup();
    ghz_effect_progress_reset();
    FUN_802215a8(2, 0, 2, 2, 2);
    FUN_802217d4((int)0x8020b5cc);   // ghz_mths_event_dispatch

    static const struct { int id; unsigned int flag; } zones[] = {
        {0x4dc, 0x20000}, {0x5e9, 0x20000}, {0x5ea, 0x20000}, {0x5ed, 0x20000},
        {0x5ef, 0x10000}, {0x5f1, 0x20000}, {0x633, 0x10000}, {0x631, 0x10000},
        {0x636, 0x20000}, {0x63a, 0x10000}, {0x63c, 0x20000}, {0x63e, 0x10000},
        {0x648, 0x20000},
    };
    for (int i = 0; i < (int)(sizeof(zones) / sizeof(zones[0])); i = i + 1) {
        int z = DAT_80611b8c + soundpad_find_zone_by_id(zones[i].id) * 0x20;
        *(unsigned int *)(z + 4) = *(unsigned int *)(z + 4) | zones[i].flag;
    }

    mths_soundzone_set_default();
}

// ---------------------------------------------------------------------------
// 0x8021aa7c  mths_close_portals_a
// Closes 2 soundpad portal connections (sets the closed bit 0x10000) between the
// zone pairs listed in DAT_8045c0d8/0x8045c0dc (8-byte stride).
// ---------------------------------------------------------------------------
void mths_close_portals_a(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0dc + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0d8 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_any_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal | 0x10000;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 2);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8021ab0c  mths_close_portals_b
// Closes 3 soundpad portal connections from the DAT_8045c0e8/0x8045c0ec pair
// table (variant of mths_close_portals_a for a later story state).
// ---------------------------------------------------------------------------
void mths_close_portals_b(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0ec + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0e8 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_any_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal | 0x10000;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 3);
    FUN_80382730();
}

// ===========================================================================
// batch_665: MTHS level story-event dispatcher, prop object-init + ticks,
// portal openers, detonation, objective proximity gate, and the vehicle/engine
// prop tick (0x8021ab9c .. 0x8021b44c). Appended to worm_mths_level.cpp.
// ===========================================================================

// Forward declarations for batch_665 functions referenced before their
// definition (mths_prop_detonate is called by mths_story_event_dispatch).
void mths_prop_detonate(int *prop);
void mths_get_prop_vertex_pos(int entity, void *out);

// batch_665 named helpers (file-local externs; generated_decls not in this TU).
extern int  *soundpad_find_active_portal(int *zoneA, int *zoneB);
extern void  sound_init_play_params(int params);
extern unsigned int sound_play_request(int *params);
extern void  sound_play_stereo_pair(double a, double b, double c);
extern void  sound_update_3d_source(double a, double b, double c, int orientPtr,
                                     void *worldPos, int bank, int flags);
extern void  sound_set_pitch(double pitch, unsigned int handle);
extern void  entity_destroy_with_event(void *entity);
extern void  entity_register_for_update(int entity, int slot, int cb);
extern void  entity_set_render_type(int entity, int renderType);
extern void  entity_get_sub_object_vertex_world_pos(void *out, int entity, int a, int b);
extern void *entity_get_world_position(int entity);
extern void  entity_apply_vertex_color_offset(int entity, int which, char r, char g,
                                              char b, int a, int c, int d);
extern void  switch_slot_reset(int block);
extern void  damage_dispatch_to_entity(double p1, double p2, double p3, long long p4,
                                        long long p5, long long p6, long long p7,
                                        long long p8, void *record);
extern void  damage_entity_receive(void);
extern int  *get_string_by_id(int id);
extern void  hud_text_submit(int playerRec, int line, unsigned int *str);
extern double anim_get_clip_end_time_of(int entity);
extern double math_sqrt(double x);

// player_ai_explosion_spawn: shared decl differs from this call site; bind by
// ROM address as a file-local alias (same address used in worm_rain_level.cpp).
typedef int (*explosionspawn2_fp)(int, int, float *, float *, int, int, int);
static const explosionspawn2_fp __mths_explosion_spawn = (explosionspawn2_fp)0x800c95d8; // player_ai_explosion_spawn

// Raw ROM helper symbols still un-named elsewhere.
extern int  FUN_8023c32c(unsigned int tag);   // entity-find-by-tag ('ROD1' etc.)
extern int  FUN_802216f0(int a, int b, int c); // mths spawn/find prop by class
extern void FUN_8022df38(long long, long long, long long, long long, long long,
                         long long, long long, long long, int);  // objective-complete handler
extern double FUN_80390324(double x);          // floor/modf helper (spin wrap)
extern double FUN_80390ae4(double t, double clipEnd); // anim time lerp helper
extern unsigned int FUN_80366414(void);        // anim playback-active predicate

// batch_665 raw data / constants
extern int    DAT_80612610;    // active player count (shared)
extern int    DAT_80611d78;    // player-record array base (shared)
extern int    DAT_80611928;    // previous-frame time raw
extern unsigned int DAT_806109c0;  // frame delta raw (shared)
extern unsigned char DAT_805015bd; // story progress byte (shared)
extern unsigned int DAT_805015c0;  // story flag word
extern float  FLOAT_806108ec;  // frame-time scale (shared)
extern double DOUBLE_80618330;
extern float  DAT_803dff90, DAT_803dff94, DAT_803dff98;  // detonation offset pos
extern float  FLOAT_806182d0;  // spin wrap base
extern float  FLOAT_806182d4;  // common 0.0f / base value
extern float  FLOAT_806182d8;  // spin period
extern float  FLOAT_806182dc;  // engine rate / stereo L
extern float  FLOAT_806182e0;  // stereo R
extern float  FLOAT_806182e4;  // proximity dist^2 threshold A
extern float  FLOAT_806182e8;  // proximity dist^2 threshold B
extern float  FLOAT_806182ec;  // damage clip A
extern float  FLOAT_806182f0;  // damage clip B
extern float  FLOAT_806182f8;  // engine time divisor
extern float  FLOAT_806182fc;  // heat-glow neutral
extern float  FLOAT_80618300;  // heat-glow slope
extern float  FLOAT_80618304;  // heat-glow lo ref
extern float  FLOAT_80618308;  // heat-glow lo span
extern float  FLOAT_8061830c;  // heat-glow hi ref
extern float  FLOAT_80618310;  // heat-glow hi span
extern float  FLOAT_80618314;  // engine vol lo clamp
extern float  FLOAT_80618318;  // engine vol hi clamp
extern float  FLOAT_8061831c;  // engine vol scale
extern float  FLOAT_80618320;  // engine 3D param
extern float  FLOAT_80618324;  // engine pitch base
extern float  FLOAT_80618328;  // engine pitch scale

extern int    LAB_8021b2c0;    // render-type label (story-gated engine render)

// ---------------------------------------------------------------------------
// 0x8021ab9c  mths_story_event_dispatch
// Story-progression event dispatcher for the MTHS objective set-piece. The
// current event stage lives at *param_9; param_10 is the sub-event id (0xf =
// "destroy"). Stages destroy their 'RODn' marker entities ('ROD1'..'ROD5'),
// open/close soundpad portals, play the stage sounds (0x687 / 0x688), submit the
// objective HUD line (0x117d) to every active player, and — at the final stage —
// deal the finishing damage to the spawned prop (class 0xc4 via FUN_802216f0).
// ---------------------------------------------------------------------------
void mths_story_event_dispatch(long long p1, double p2, double p3, long long p4,
                               long long p5, long long p6, long long p7, long long p8,
                               int *param_9, int subEvent)
{
    int stage;
    void *marker;
    int prop;
    int pi;
    int off;
    int soundA[12];
    int soundB[12];
    int dmgRecord;       // local_68
    int dmgOwnerA;       // local_64
    int dmgOwnerB;       // local_60
    float dmgAmount;     // local_48

    stage = *param_9;
    if (stage == 2) {
        if ((subEvent == 0xf) &&
            (marker = (void *)FUN_8023c32c(0x444f5233), marker != (void *)0)) {  // 'ROD3'
            entity_destroy_with_event(marker);
        }
    } else if (stage < 2) {
        if (stage == 0) {
            if (subEvent == 0xf) {
                marker = (void *)FUN_8023c32c(0x444f5231);  // 'ROD1'
                if (marker != (void *)0) {
                    entity_destroy_with_event(marker);
                }
                mths_close_portals_a();
                sound_init_play_params((int)soundB);
                soundB[0] = 0x687;
                sound_play_request(soundB);
            }
        } else if (-1 < stage) {   // stage == 1
            if (subEvent == 0xf) {
                marker = (void *)FUN_8023c32c(0x444f5232);  // 'ROD2'
                if (marker != (void *)0) {
                    entity_destroy_with_event(marker);
                }
            } else if (((subEvent == 0) || (subEvent == 1)) &&
                       ((*(unsigned short *)(param_9 + 2) & 0x8000) == 0)) {
                off = 0;
                for (pi = 0; pi < DAT_80612610; pi = pi + 1) {
                    if (-1 < *(int *)(DAT_80611d78 + off + 4)) {
                        unsigned int *str = (unsigned int *)get_string_by_id(0x117d);
                        hud_text_submit(*(int *)(DAT_80611d78 + off + 4), 3, str);
                    }
                    off = off + 0x141c;
                }
            }
        }
    } else if (stage == 4) {
        if ((subEvent == 0xf) &&
            (marker = (void *)FUN_802216f0(4, 0xc4, 0), marker != (void *)0)) {
            mths_prop_detonate((int *)marker);
        }
    } else if ((stage < 4) && (subEvent == 0xf)) {   // stage == 3
        prop = FUN_802216f0(stage, 0xc3, 0);
        marker = (void *)FUN_8023c32c(0x444f5235);   // 'ROD5'
        if (marker != (void *)0) {
            entity_destroy_with_event(marker);
        }
        marker = (void *)FUN_8023c32c(0x444f5234);   // 'ROD4'
        if (marker != (void *)0) {
            entity_destroy_with_event(marker);
        }
        if (prop != 0) {
            switch_slot_reset((int)&dmgRecord);
            dmgAmount = (float)((double)FLOAT_806182d4 + (double)*(float *)(prop + 0x1cc));
            dmgOwnerA = *(int *)(DAT_80611d78 + 0x14);
            dmgRecord = prop;
            dmgOwnerB = dmgOwnerA;
            damage_dispatch_to_entity((double)FLOAT_806182d4, p2, p3, p4, p5, p6, p7, p8,
                                      &dmgRecord);
            (void)dmgAmount;
        }
        sound_init_play_params((int)soundA);
        soundA[0] = 0x688;
        sound_play_request(soundA);
        mths_close_portals_b();
    }
    (void)p1;
}

// ---------------------------------------------------------------------------
// 0x8021adc4  mths_spin_prop_tick  (entity update cb)
// Advances a spinning prop's angle (ent+0x70) by rate ent+0xd0 * frame-time,
// wrapping into [FLOAT_806182d0, +FLOAT_806182d8) via the floor helper.
// ---------------------------------------------------------------------------
void mths_spin_prop_tick(int ent)
{
    double angle;
    double wrapped;

    angle = (double)(*(float *)(ent + 0x70) + *(float *)(ent + 0xd0) * FLOAT_806108ec);
    wrapped = FUN_80390324((double)((float)(angle - (double)FLOAT_806182d0) / FLOAT_806182d8));
    *(float *)(ent + 0x70) = -(float)((double)FLOAT_806182d8 * (double)(float)wrapped - angle);
}

// ---------------------------------------------------------------------------
// 0x8021ae34  mths_open_portals_a
// Sets a scene-node flag (bit 2) on the 'ROD1' marker then opens 2 soundpad
// portals (clears closed bit 0x10000) from the DAT_8045c0d8/0dc pair table.
// ---------------------------------------------------------------------------
void mths_open_portals_a(void)
{
    int marker;
    int i;
    int off;

    FUN_803826e4();
    marker = FUN_8023c32c(0x444f5231);   // 'ROD1'
    *(unsigned int *)(*(int *)(marker + 4) + 0x30) =
        *(unsigned int *)(*(int *)(marker + 4) + 0x30) | 2;
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0dc + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0d8 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_active_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal & 0xfffeffff;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 2);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8021aee0  mths_open_portals_b
// Opens 3 soundpad portals (clears closed bit 0x10000) from the DAT_8045c0e8/0ec
// pair table.
// ---------------------------------------------------------------------------
void mths_open_portals_b(void)
{
    int i;
    int off;

    FUN_803826e4();
    i = 0;
    off = 0;
    do {
        int zoneB = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0ec + off));
        int base = DAT_80611b8c;
        int zoneA = soundpad_find_zone_by_id(*(int *)((int)&DAT_8045c0e8 + off));
        unsigned int *portal = (unsigned int *)soundpad_find_active_portal(
            (int *)(DAT_80611b8c + zoneA * 0x20), (int *)(base + zoneB * 0x20));
        if (portal != (unsigned int *)0) {
            *portal = *portal & 0xfffeffff;
        }
        i = i + 1;
        off = off + 8;
    } while (i < 3);
    FUN_80382730();
}

// ---------------------------------------------------------------------------
// 0x8021af70  mths_prop_detonate
// Blows up a prop entity: spawns explosion type 0x21 at the prop's sub-object
// position (offset DAT_803dff90..98), unregisters all its update callbacks and
// render type, plays the detonation stereo sound, and destroys it.
// ---------------------------------------------------------------------------
void mths_prop_detonate(int *prop)
{
    float pos[3];   // local_18/14/10

    pos[0] = DAT_803dff90;
    pos[1] = DAT_803dff94;
    pos[2] = DAT_803dff98;
    __mths_explosion_spawn(0x21, 0x17, (float *)(prop + 0x19), pos, 0, (int)prop, 1);
    entity_register_for_update((int)prop, 3, 0);
    entity_register_for_update((int)prop, 2, 0);
    entity_register_for_update((int)prop, 10, 0);
    entity_set_render_type((int)prop, 0);
    sound_play_stereo_pair((double)FLOAT_806182dc, (double)FLOAT_806182d4,
                           (double)FLOAT_806182e0);
    entity_destroy_with_event(prop);
}

// ---------------------------------------------------------------------------
// 0x8021b030  mths_get_prop_vertex_pos
// Thin wrapper: fetches a prop's sub-object vertex-0 world position.
// ---------------------------------------------------------------------------
void mths_get_prop_vertex_pos(int entity, void *out)
{
    entity_get_sub_object_vertex_world_pos(out, entity, 0, 0);
}

// ---------------------------------------------------------------------------
// 0x8021b064  mths_objective_proximity_check
// Objective gate (story stage 3 only). Validates that a player is carrying the
// objective item (owner slot ctx[2] -> object) and, when the relevant sub-event
// (0x17/0xe) fires, checks whether either of two prop sub-object vertices is
// within threshold distance of the carried item (ctx[10]); on success latches the
// completion value into ctx[8], otherwise nags the owner with objective text
// 0x11a4. Special case object state 2 routes to the objective-complete handler.
// Returns 1 when the objective condition is met, else 0.
// ---------------------------------------------------------------------------
unsigned int mths_objective_proximity_check(long long p1, double p2, double p3, long long p4,
                                            long long p5, long long p6, long long p7,
                                            long long p8, int *ctx)
{
    unsigned int result;
    unsigned int *str;
    int owner;
    int object;
    float *item;
    int propEnt;
    float a[3];     // local_4c/48/44 (vertex 0)
    float b[3];     // local_58/54/50 (vertex 1)
    double dz, dy, dx;

    propEnt = *ctx;
    object = 0;
    if (DAT_805015bd != '\x03') {
        return 0;
    }
    owner = ctx[2];
    if ((owner == 0) || (*(int *)(owner + 0x150) == 0) ||
        (-1 < *(int *)(*(int *)(owner + 0x150) + 4))) {
        if ((owner != 0) && (*(int *)(owner + 0x20) == 8)) {
            object = *(int *)(owner + 0x7c);
        }
        if (object == 0) {
            return 0;
        }
        if (DAT_80612610 <= *(int *)(object + 8)) {
            return 1;
        }
        if (*(int *)(object + 0x84) == 2) {
            FUN_8022df38(p1, p2, p3, p4, p5, p6, p7, p8, *(int *)(owner + 0x150));
            return 0;
        }
        if ((ctx[6] == 0x17) && (ctx[5] == 0xe)) {
            mths_get_prop_vertex_pos(propEnt, a);
            item = (float *)ctx[10];
            dx = (double)(a[0] - item[0]);
            dy = (double)(a[1] - item[1]);
            dz = (double)(a[2] - item[2]);
            entity_get_sub_object_vertex_world_pos(b, propEnt, 0, 1);
            item = (float *)ctx[10];
            if ((ABS((float)(dz * dz + (double)(float)(dx * dx + (double)(float)(dy * dy)))) <=
                 FLOAT_806182e4) ||
                (ABS((b[2] - item[2]) * (b[2] - item[2]) +
                     (b[0] - item[0]) * (b[0] - item[0]) +
                     (b[1] - item[1]) * (b[1] - item[1])) <= FLOAT_806182e8)) {
                result = 1;
                ctx[8] = (int)(FLOAT_806182d4 + *(float *)(propEnt + 0x1cc));
            } else {
                if (-1 < *(int *)(*(int *)(ctx[2] + 0x150) + 4)) {
                    str = (unsigned int *)get_string_by_id(0x11a4);
                    hud_text_submit(*(int *)(*(int *)(ctx[2] + 0x150) + 4), 3, str);
                }
                result = 0;
            }
        } else {
            str = (unsigned int *)get_string_by_id(0x11a4);
            hud_text_submit(*(int *)(*(int *)(ctx[2] + 0x150) + 4), 3, str);
            result = 0;
        }
    } else {
        result = 1;
        ctx[8] = (int)(FLOAT_806182d4 + *(float *)(propEnt + 0x1cc));
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x8021b2e0  mths_prop_object_init
// Object-init dispatcher for the MTHS prop entity classes (id at ent+0x94):
//   0xc4  -> spinning engine prop: flags 0x9c |= 0x30, 0x28 |= 0x70, seed heat
//            (0x1cc/0x1d8), spin rate ent+0xd0, register mths_spin_prop_tick (slot
//            3); when story==3 also set render = LAB_8021b2c0 (engine render).
//   0xc3  -> damageable prop: render = damage_entity_receive, seed hp/timers.
//   0xbf..0xc2 -> toggle a scene-node flag (set then clear bit 4).
//   0x108/0x109 -> set render bit 0x4000 (mask off 0xC000 first).
// ---------------------------------------------------------------------------
void mths_prop_object_init(int ent)
{
    unsigned short cls;
    float base;
    float rate;

    base = FLOAT_806182d4;
    cls = *(unsigned short *)(ent + 0x94);
    if (cls == 0xc4) {
        *(unsigned int *)(ent + 0x9c) = *(unsigned int *)(ent + 0x9c) | 0x30;
        rate = FLOAT_806182dc;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x70;
        *(float *)(ent + 0x1d8) = base;
        *(float *)(ent + 0x1cc) = base;
        *(unsigned char *)(ent + 0x32) = 2;
        *(float *)(ent + 0xd0) = rate;
        entity_register_for_update(ent, 3, (int)0x8021adc4);   // mths_spin_prop_tick
        if (DAT_805015bd == '\x03') {
            entity_set_render_type(ent, (int)&LAB_8021b2c0);
        }
    } else if (cls < 0xc4) {
        if (cls < 0xc3) {
            if (0xbe < cls) {   // 0xbf..0xc2
                *(unsigned int *)(*(int *)(ent + 4) + 0x30) =
                    *(unsigned int *)(*(int *)(ent + 4) + 0x30) | 2;
                *(unsigned int *)(*(int *)(ent + 4) + 0x30) =
                    *(unsigned int *)(*(int *)(ent + 4) + 0x30) & 0xfffffffb;
            }
        } else {   // 0xc3
            entity_set_render_type(ent, (int)0x80090f7c);   // damage_entity_receive
            rate = FLOAT_806182f0;
            base = FLOAT_806182ec;
            *(float *)(ent + 0x1d8) = FLOAT_806182ec;
            *(float *)(ent + 0x1cc) = base;
            *(float *)(ent + 0x1dc) = rate;
        }
    } else if ((cls < 0x10a) && (0x107 < cls)) {   // 0x108/0x109
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffff3fff;
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x4000;
    }
}

// ---------------------------------------------------------------------------
// 0x8021b410  mths_open_portals_story
// Once story progress reaches stage 3, opens both portal sets (a + b).
// ---------------------------------------------------------------------------
void mths_open_portals_story(void)
{
    if (DAT_805015bd == '\x03') {
        mths_open_portals_a();
        mths_open_portals_b();
    }
}

// ---------------------------------------------------------------------------
// 0x8021b44c  mths_engine_prop_tick  (entity update cb)
// Vehicle/engine prop per-frame tick. When its animation is playing, advances the
// anim playback clock (ent+0x4->0xd4) proportional to the frame/previous-frame
// time ratio and the object's speed word (obj+0x10). Then, unless masked by the
// 'GBOB' sub-object tag, computes a heat-glow vertex-colour offset from the anim
// clock (piecewise around FLOAT_806182fc) and applies it to vertex sets 0/1.
// Finally drives the engine 3D loop sound (bank 0x1570) with a speed-scaled
// volume (clamped) and pitch.
// ---------------------------------------------------------------------------
void mths_engine_prop_tick(int ent)
{
    int obj;
    float clock;
    int glow;
    char g;
    double ratio;
    double clipEnd;
    double speedNeg;
    double speed;
    void *worldPos;
    double vol;
    double volClamped;

    obj = *(int *)(ent + 0x7c);

    if ((FUN_80366414() != 0) && ((DAT_805015c0 & 0x40) == 0)) {
        clipEnd = anim_get_clip_end_time_of(ent);
        ratio = (double)FUN_80390ae4(
            (double)((float)((double)CONCAT44(0x43300000, DAT_80611928 ^ 0x80000000) -
                             DOUBLE_80618330) /
                     (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                             DOUBLE_80618330)),
            clipEnd);
        double part = (double)(float)ratio;
        clipEnd = anim_get_clip_end_time_of(ent);
        double clipEnd2 = anim_get_clip_end_time_of(ent);
        speedNeg = (double)(CONCAT44(0x43300000, *(int *)(obj + 0x10)) ^ 0x80000000);
        ratio = (double)FUN_80390ae4(
            (double)(float)(part +
                            (double)((float)((double)(float)(speedNeg - DOUBLE_80618330) *
                                             clipEnd2) / FLOAT_806182f8)),
            clipEnd);
        *(float *)(*(int *)(ent + 4) + 0xd4) = (float)ratio;
    }

    if ((*(int *)(ent + 0x2d8) == 0) ||
        (*(int *)(*(int *)(ent + 0x2d8) + 4) != 0x424f4247)) {   // 'GBOB'
        clock = *(float *)(*(int *)(ent + 4) + 0xd4);
        glow = 0;
        if (FLOAT_806182fc <= clock) {
            if (FLOAT_8061830c < clock) {
                int v = -(int)((FLOAT_80618300 * (clock - FLOAT_8061830c)) / FLOAT_80618310);
                glow = -0x7f;
                if (-0x80 < v) {
                    glow = v;
                }
            }
        } else {
            int v = (int)((FLOAT_80618300 * (clock - FLOAT_80618304)) / FLOAT_80618308) + -0x7f;
            glow = -0x7f;
            if (-0x80 < v) {
                glow = v;
            }
        }
        if (glow < 0) {
            g = (char)glow;
            entity_apply_vertex_color_offset(ent, 0, g, g, g, 0, 0, 0);
            entity_apply_vertex_color_offset(ent, 1, g, g, g, 0, 0, 0);
        }
    }

    *(unsigned int *)(ent + 0x18) = *(unsigned int *)(ent + 0x18) | 2;

    if (*(int *)(ent + 0x1a0) == 0) {
        speed = math_sqrt((double)(*(float *)(ent + 0x100) * *(float *)(ent + 0x100) +
                                   *(float *)(ent + 0xf8) * *(float *)(ent + 0xf8) +
                                   *(float *)(ent + 0xfc) * *(float *)(ent + 0xfc)));
        speedNeg = (double)(CONCAT44(0x43300000, DAT_806109c0) ^ 0x80000000);
        vol = (double)((float)((double)(float)(speedNeg - DOUBLE_80618330) * speed) /
                       FLOAT_806182f8);
        volClamped = vol;
        if ((double)FLOAT_80618318 < vol) {
            volClamped = (double)FLOAT_80618318;
        }
        if ((double)FLOAT_80618314 <= volClamped) {
            if ((double)FLOAT_80618318 < vol) {
                vol = (double)FLOAT_80618318;
            }
        } else {
            vol = (double)FLOAT_80618314;
        }
        worldPos = entity_get_world_position(ent);
        sound_update_3d_source((double)(float)((double)FLOAT_8061831c * vol),
                               (double)FLOAT_80618320, (double)FLOAT_80618318,
                               ent + 0x22c, worldPos, 0x1570, 0);
        if (-1 < (int)*(unsigned int *)(ent + 0x22c)) {
            sound_set_pitch((double)(float)((double)FLOAT_80618328 * vol +
                                            (double)FLOAT_80618324),
                            *(unsigned int *)(ent + 0x22c));
        }
    } else {
        *(int *)(ent + 0x1a0) = 0;
    }
}

// ===========================================================================
// batch_666: MTHS animated conveyor/rotor prop (class 0x11c) + a level ambient
// 3D loop driver and the objective-complete/match-end handler
// (0x8021b768 .. 0x8021bd24). Appended to worm_mths_level.cpp.
// ===========================================================================

// Forward declarations for batch_666 functions referenced before definition.
double mths_player_zone_volume(int playerRec);
void   mths_ambient_loop_tick(void);

// batch_666 named helpers (file-local externs; generated_decls not in this TU).
extern void  anim_blend_eval_flagged(int entity);
extern void  memset_wrapper(int dst, int val, int n);
extern void  sound_clear_bank_ref(int bank);
extern int   entity_model_cache_alloc(long long, double, double, long long, long long,
                                       long long, long long, long long, int ent, int obj,
                                       int size, int, int, int, int, int);
extern void  anim_play_scaled(double a, double b);
extern double anim_get_clip_end_time_of(int entity);
extern void  anim_active_slot_set_speed(double speed, int entity);
extern int   scalar_step_toward_snap(double target, double step, float *value);
extern int   session_get_state_flag(void);
extern void  match_process_end(long long, double, double, long long, long long,
                               long long, long long, long long, int);

// mem_alloc shared decl is (size,type); this is the wide 16-arg ABI form, so
// bind by ROM address as a file-local alias.
typedef int (*memalloc16_fp)(long long, double, double, long long, long long,
                             long long, long long, long long, int, int, int, int,
                             int, int, int, int);
static const memalloc16_fp __mths_mem_alloc = (memalloc16_fp)0x80006c44; // mem_alloc

// FUN_80387fb8: sprintf-like string format helper (into a stack buffer).
extern int FUN_80387fb8(long long, double, double, long long, long long, long long,
                        long long, long long, int outBuf, char *fmt, int arg,
                        int, int, int, int, int);

// batch_666 raw data / constants
extern int    DAT_80612858;    // conveyor/rotor state block (0x10)
extern int    DAT_80611928;    // previous-frame time raw (shared)
extern unsigned char DAT_805015be; // secondary story-progress byte
extern unsigned int  DAT_805015c0; // story flag word (shared)
extern unsigned int  DAT_805015c4; // objective bitfield
extern int    DAT_806108e4;    // ambient cadence enable (shared)
extern int    DAT_80610e90;    // ambient loop 3D source position record
extern float  FLOAT_80612860;  // ambient loop current volume (mutable global)
extern double DOUBLE_80618368;
extern float  FLOAT_80618338;  // conveyor initial speed
extern float  FLOAT_8061833c;  // conveyor GBOB anim rate
extern float  FLOAT_80618340;  // conveyor GBOB base speed
extern float  FLOAT_80618344;  // conveyor speed snap step
extern float  FLOAT_80618348;  // ambient volume floor
extern float  FLOAT_8061834c;  // ambient volume level A / step scale
extern float  FLOAT_80618350;  // ambient volume level B
extern float  FLOAT_80618354;  // ambient volume level C
extern float  FLOAT_80618358;  // ambient volume level D
extern float  FLOAT_8061835c;  // ambient step divisor
extern float  FLOAT_80618360;  // ambient 3D param

// ---------------------------------------------------------------------------
// 0x8021b768  mths_conveyor_move_tick  (entity update cb, 'GBOB' variant)
// Evaluates the entity's flagged animation blend, then integrates its linear
// velocity (ent+0xa8/0xac/0xb0) into its position (ent+0x64/0x68/0x6c).
// ---------------------------------------------------------------------------
void mths_conveyor_move_tick(int ent)
{
    anim_blend_eval_flagged(ent);
    *(float *)(ent + 0x64) = *(float *)(ent + 0x64) + *(float *)(ent + 0xa8);
    *(float *)(ent + 0x68) = *(float *)(ent + 0x68) + *(float *)(ent + 0xac);
    *(float *)(ent + 0x6c) = *(float *)(ent + 0x6c) + *(float *)(ent + 0xb0);
}

// ---------------------------------------------------------------------------
// 0x8021b7c4  mths_conveyor_alloc
// Allocates the 0x10-byte conveyor/rotor shared state (DAT_80612858) and clears
// the conveyor sound-bank references (0x156f/0x1570/0x5f3/0x5f4/0x5f5).
// ---------------------------------------------------------------------------
void mths_conveyor_alloc(long long p1, double p2, double p3, long long p4, long long p5,
                         long long p6, long long p7, long long p8, int a9, int a10,
                         int a11, int a12, int a13, int a14, int a15, int a16)
{
    DAT_80612858 = __mths_mem_alloc(p1, p2, p3, p4, p5, p6, p7, p8, 0x10, 1, a11, a12,
                                    a13, a14, a15, a16);
    sound_clear_bank_ref(0x156f);
    sound_clear_bank_ref(0x1570);
    sound_clear_bank_ref(0x5f3);
    sound_clear_bank_ref(0x5f4);
    sound_clear_bank_ref(0x5f5);
}

// ---------------------------------------------------------------------------
// 0x8021b818  mths_conveyor_init
// Zeroes the conveyor state block and seeds its speed (+4) to FLOAT_80618338.
// ---------------------------------------------------------------------------
void mths_conveyor_init(void)
{
    memset_wrapper(DAT_80612858, 0, 8);
    *(float *)(DAT_80612858 + 4) = FLOAT_80618338;
}

// ---------------------------------------------------------------------------
// 0x8021b850  mths_conveyor_object_init
// Object-init dispatcher for the conveyor set-piece classes. 0x9e/0x9f -> plain
// render props (flag 0x40000000, clip 0xd5). 0x11c -> the animated conveyor/rotor
// prop: when the story gate is inactive it is hidden (clear bits); otherwise it
// allocates a 0x14-byte object record and, for the 'GBOB' sub-object, registers
// the move-tick (mths_conveyor_move_tick) with a fixed anim, else registers the
// generic anim-blend tick with a speed derived from the frame clock / conveyor
// state. Both variants also register mths_engine_prop_tick (slot 3) and mark the
// entity active (bit 0x4000000).
// ---------------------------------------------------------------------------
int mths_conveyor_object_init(long long p1, double p2, double p3, long long p4, long long p5,
                              long long p6, long long p7, long long p8, int ent, int obj,
                              int a11, int a12, int a13, int a14, int a15, int a16)
{
    unsigned short cls;
    bool storyGate;
    int rec;
    int handleIdx;
    double clipEnd;
    double gbobT;

    cls = *(unsigned short *)(ent + 0x94);
    if (cls == 0x9f) {
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40000000;
        *(int *)(ent + 0x34) = 0xd5;
        return 0;
    }
    if (cls < 0x9f) {
        if (cls < 0x9e) {
            return 0;
        }
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x40000000;
        *(int *)(ent + 0x34) = 0xd5;
        return 0;
    }
    if (cls != 0x11c) {
        return 0;
    }

    storyGate = false;
    if ((DAT_805015bd == 5) ||
        (((DAT_805015bd < 5 && DAT_805015bd < 3) && DAT_805015bd != 0))) {
        storyGate = true;
    }
    if (DAT_805015be < 0xb) {
        if ((DAT_805015be < 8) && (5 <= DAT_805015be)) {
            storyGate = true;
        }
    } else if (DAT_805015be < 0xd) {
        storyGate = true;
    }

    if (storyGate) {
        *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) & 0xffffff9e;
        return 0;
    }

    rec = entity_model_cache_alloc(p1, p2, p3, p4, p5, p6, p7, p8, ent, obj, 0x14,
                                   a12, a13, a14, a15, a16);
    *(int *)(ent + 0x7c) = rec;
    memset_wrapper(*(int *)(ent + 0x7c), 0, 0x14);
    rec = *(int *)(ent + 0x7c);
    if (*(int *)(obj + 4) == 0x424f4247) {   // 'GBOB'
        anim_play_scaled((double)FLOAT_80618314, (double)FLOAT_80618318);
        entity_register_for_update(ent, 2, (int)0x8021b768);   // mths_conveyor_move_tick
        *(int *)(rec + 0x10) = -1;
    } else {
        if ((DAT_805015c0 & 0x40) == 0) {
            anim_play_scaled((double)FLOAT_80618314, (double)FLOAT_80618318);
            clipEnd = anim_get_clip_end_time_of(ent);
            anim_active_slot_set_speed(
                (double)(float)((double)CONCAT44(0x43300000,
                    (DAT_80611928 - (DAT_80611928 / (int)clipEnd) * (int)clipEnd) ^ 0x80000000) -
                    DOUBLE_80618330),
                ent);
        } else {
            anim_play_scaled((double)FLOAT_80618314, (double)FLOAT_8061833c);
            clipEnd = anim_get_clip_end_time_of(ent);
            gbobT = (double)(CONCAT44(0x43300000, *(int *)DAT_80612858) ^ 0x80000000);
            anim_active_slot_set_speed(
                (double)(FLOAT_80618340 +
                         (float)((double)(float)(gbobT - DOUBLE_80618330) * clipEnd) /
                         FLOAT_806182f8),
                ent);
        }
        entity_register_for_update(ent, 2, (int)0x80020034);   // anim_blend_eval_flagged
        handleIdx = *(int *)DAT_80612858;
        *(int *)DAT_80612858 = handleIdx + 1;
        *(int *)(rec + 0x10) = handleIdx;
    }
    *(int *)(ent + 0x1a0) = 1;
    entity_register_for_update(ent, 3, (int)0x8021b44c);       // mths_engine_prop_tick
    *(unsigned int *)(ent + 0x28) = *(unsigned int *)(ent + 0x28) | 0x4000000;
    return 0;
}

// ---------------------------------------------------------------------------
// 0x8021baec  mths_conveyor_speed_tick
// Ramps the conveyor speed (DAT_80612858+4) down each frame (frame-time scaled);
// once it drops below FLOAT_80618314 it snaps back up by FLOAT_80618344 and plays
// the conveyor loop sound (bank 0x156f). Gated on the ambient cadence enable.
// ---------------------------------------------------------------------------
void mths_conveyor_speed_tick(void)
{
    float thresh;
    int params[12];

    thresh = FLOAT_80618314;
    if (DAT_806108e4 != 0) {
        *(float *)(DAT_80612858 + 4) =
            -((FLOAT_80618318 /
               (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                       DOUBLE_80618330)) *
                  FLOAT_806108ec -
              *(float *)(DAT_80612858 + 4));
        if (*(float *)(DAT_80612858 + 4) <= thresh) {
            *(float *)(DAT_80612858 + 4) = *(float *)(DAT_80612858 + 4) + FLOAT_80618344;
            sound_init_play_params((int)params);
            params[0] = 0x156f;
            sound_play_request(params);
        }
    }
}

// ---------------------------------------------------------------------------
// 0x8021bb8c  mths_ambient_preload  — clears ambient sound banks 0x3b and 0x51.
// ---------------------------------------------------------------------------
void mths_ambient_preload(void)
{
    sound_clear_bank_ref(0x3b);
    sound_clear_bank_ref(0x51);
}

// ---------------------------------------------------------------------------
// 0x8021bbcc  mths_player_zone_volume
// Maps a player's current soundpad zone type (node+0x34) to the ambient loop
// volume level for that player. Zone types 0x28/0x2c -> highest (D), 0x2d/0x2f ->
// C, 0x2e/0x30 -> B, 0x31..0x34 -> the step-scale level, everything else -> floor.
// ---------------------------------------------------------------------------
double mths_player_zone_volume(int playerRec)
{
    switch (*(int *)(*(int *)(playerRec + 0x14) + 0x34)) {
    case 0x28:
    case 0x2c:
        return (double)FLOAT_80618358;
    default:
        return (double)FLOAT_80618348;
    case 0x2d:
    case 0x2f:
        return (double)FLOAT_80618354;
    case 0x2e:
    case 0x30:
        return (double)FLOAT_80618350;
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
        return (double)FLOAT_8061834c;
    }
}

// ---------------------------------------------------------------------------
// 0x8021bc20  mths_ambient_loop_tick
// Level ambient 3D loop driver (bank 0x3b). Takes the maximum per-player zone
// volume (mths_player_zone_volume over active players), steps the current loop
// volume (FLOAT_80612860) toward it at a frame-time-scaled rate, and updates the
// 3D source at the loop position record (DAT_80610e90).
// ---------------------------------------------------------------------------
void mths_ambient_loop_tick(void)
{
    double target;
    double v;
    int i;
    int off;

    target = (double)FLOAT_80618348;
    off = 0;
    for (i = 0; i < DAT_80612610; i = i + 1) {
        if ((-1 < *(int *)(DAT_80611d78 + off + 4)) &&
            (v = mths_player_zone_volume(DAT_80611d78 + off), target < v)) {
            target = v;
        }
        off = off + 0x141c;
    }
    scalar_step_toward_snap(
        target,
        (double)((FLOAT_8061834c /
                  (float)((double)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) -
                          DOUBLE_80618368)) /
                 FLOAT_8061835c),
        &FLOAT_80612860);
    sound_update_3d_source((double)FLOAT_80612860, (double)FLOAT_80618360,
                           (double)FLOAT_8061834c, (int)&DAT_80610e90, 0, 0x3b, 0);
}

// ---------------------------------------------------------------------------
// 0x8021bd00  mths_ambient_loop_render  (wrapper -> mths_ambient_loop_tick)
// ---------------------------------------------------------------------------
void mths_ambient_loop_render(void)
{
    mths_ambient_loop_tick();
}

// ---------------------------------------------------------------------------
// 0x8021bd24  mths_objective_complete_event
// Objective-complete / match-end handler (event 3). Formats a win/lose message
// (string 0x15ea when the triggering player has a valid owner record, else the
// alternate 0x15eb) with the prop's value word, broadcasts it as a line-4 HUD
// notification to every session player, sets the objective bit (DAT_805015c4 |=
// 0x10000), and ends the match. Returns -1 to consume the event.
// ---------------------------------------------------------------------------
unsigned int mths_objective_complete_event(long long p1, double p2, double p3, long long p4,
                                           long long p5, long long p6, long long p7,
                                           long long p8, int ent, int event, int ctx,
                                           int a12, int a13, int a14, int a15, int a16)
{
    int obj;
    int i;
    int n;
    char *fmt;
    unsigned int msgBuf[65];

    if (event != 3) {
        return 0;
    }
    obj = *(int *)(ent + 0x7c);
    if ((*(int *)(ctx + 4) == 0) || (*(int *)(*(int *)(ctx + 4) + 0x150) == 0)) {
        fmt = (char *)get_string_by_id(0x15eb);
    } else {
        fmt = (char *)get_string_by_id(0x15ea);
    }
    FUN_80387fb8(p1, p2, p3, p4, p5, p6, p7, p8, (int)msgBuf, fmt,
                 *(int *)(obj + 0x70), a12, a13, a14, a15, a16);
    for (i = 0; (n = session_get_state_flag(), i < n); i = i + 1) {
        hud_text_submit(i, 4, msgBuf);
    }
    DAT_805015c4 = DAT_805015c4 | 0x10000;
    match_process_end(p1, p2, p3, p4, p5, p6, p7, p8, 0);
    return 0xffffffff;
}
