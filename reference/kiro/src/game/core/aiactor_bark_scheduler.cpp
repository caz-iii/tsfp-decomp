// Address range: 0x801432dc - 0x801444b0 | batch_407 (+ 0x8014403c-0x801444b0 batch_408) | 2026-09-05
//
// AI actor bark / dialogue SCHEDULER. Sits on top of the bark playback layer
// (aiactor_bark.cpp, batch_406). Where that file plays an individual line,
// this file decides which line should play, when, and at what priority:
//
//   * aiactor_bark_release_samples (0x801432dc) - per-mode teardown that drops
//     the sound-bank references for every bark line the current game mode
//     (DAT_80612614) can use. The line-id tables live at &DAT_8040dbd8 ..
//     &DAT_8040de9c, each preceded by its count word.
//   * aiactor_bark_request_talk (0x80143754) - queue a "talk" bark for an actor
//     that has a bark def for the current mode (records the actor handle in
//     DAT_80612154 + resets its cooldown FLOAT_8061214c).
//   * aiactor_bark_death_line_for_mode (0x80143798) - mode -> death/secondary
//     line id (0x1ac5 for mode 4, 0x108d for modes 5..6, else none).
//   * aiactor_bark_request_secondary (0x801437d8) - queue the mode death line
//     for an actor once the secondary cooldown FLOAT_80612148 has elapsed.
//   * aiactor_bark_reset_state (0x8014382c) - clear all scheduler slots/handles
//     and set per-mode bark volume params.
//   * aiactor_bark_scheduler_tick (0x801438b8) - per-frame driver: decay the
//     two cooldown timers, and when a queued actor's timer elapses resolve the
//     actor (trigger-pool DAT_80611c24, 0x2e8 stride, generation-checked via
//     +0x2c), pick a random line from its bark def, and dispatch through
//     aiactor_play_bark_global / aiactor_play_bark_3d.
//   * aiactor_bark_pick_random_event (0x80143ab4) - roll a random event id in
//     [lo,hi] and, respecting the priority table &DAT_8040e158/&DAT_8040e15c,
//     forward the winner to aiactor_bark_select_event.
//   * aiactor_bark_queue_insert (0x80143bd4) - push a (handle,priority) pair
//     into the 4-slot pending queue DAT_80588c78/DAT_80588c7c.
//   * aiactor_bark_select_event (0x80143c50) - arbitrate a candidate event id
//     against the currently-playing bark + priorities + cooldown, committing
//     the winner to DAT_80612120.
//   * aiactor_bark_combat_triggers (0x80143e10) - per-frame combat-relationship
//     driver (from character_on_death_handler): inspects an actor pair and
//     fires taunt / reaction / kill barks based on combat state.
//
// Priority table entry (stride 0xc at &DAT_8040e158): [0] (+0x00) priority /
// max-count field tested by select_event; [1] (+0x04, i.e. &DAT_8040e15c[k*3])
// the comparison weight used by pick_random_event; [2] (+0x08) unused here.

#include "common.h"
#include "generated_decls.h"   // aiactor_play_bark_*, sound_*, player_ai_is_story_mode, etc.

// ---- helpers not (correctly) covered by the shared decls -------------------
extern "C" {
// Packed gpr save/restore frame helpers (Ghidra models a pointer-ish return).
extern int  __aiactor_frame_begin(void);  // FUN_803826f0
extern void __aiactor_frame_end(void);    // FUN_8038273c
}

// generated_decls declares rng_counter_tick as returning void, but the real ABI
// returns a u32 counter. Obtain the value via a typed function-pointer cast so
// we don't conflict with the shared prototype.
static inline unsigned int __rng_u32(void)
{
    return ((unsigned int (*)(void))(void*)&rng_counter_tick)();
}

// ---- raw data symbols (per-file externs) -----------------------------------
extern int   DAT_80612614;    // current game mode

// Per-mode bark line-id tables (count word immediately precedes the array).
extern int   DAT_8040dbd8;  extern int DAT_8040dbdc;   // mode 2
extern int   DAT_8040dc04;  extern int DAT_8040dc08;   // mode 4
extern int   DAT_8040dc30;  extern int DAT_8040dc34;   // mode 0x12
extern int   DAT_8040dc5c;  extern int DAT_8040dc60;   // mode 0xb (a)
extern int   DAT_8040dc88;  extern int DAT_8040dc8c;   // mode 0xb (b)
extern int   DAT_8040dcb4;  extern int DAT_8040dcb8;   // mode 6
extern int   DAT_8040dce0;  extern int DAT_8040dce4;   // mode 5 (a)
extern int   DAT_8040dd0c;  extern int DAT_8040dd10;   // mode 5 (b)
extern int   DAT_8040dd38;  extern int DAT_8040dd3c;   // mode 0x1a (a)
extern int   DAT_8040dd64;  extern int DAT_8040dd68;   // mode 0x1a (b)
extern int   DAT_8040dd90;  extern int DAT_8040dd94;   // mode 0xe
extern int   DAT_8040ddbc;  extern int DAT_8040ddc0;   // mode 9
extern int   DAT_8040dde8;  extern int DAT_8040ddec;   // mode 0x51 (a)
extern int   DAT_8040de14;  extern int DAT_8040de18;   // mode 0x51 (b)
extern int   DAT_8040de40;  extern int DAT_8040de44;   // mode 1
extern int   DAT_8040de6c;  extern int DAT_8040de70;   // mode 0x11
extern int   DAT_8040de98;  extern int DAT_8040de9c;   // mode 0x16

// Event priority table (stride 0xc).
extern int   DAT_8040e158;    // priority/max field [k].+0x00
extern int   DAT_8040e15c;    // weight field       [k].+0x04 (== &DAT_8040e158 + 4)

// Scheduler state.
extern int   DAT_80612154;    // queued "talk" actor handle
extern int   DAT_80612150;    // queued "secondary/death" actor handle
extern int   DAT_80612120;    // currently-selected event id (+ arbitration slot)
extern int   DAT_80612124;    // currently-playing event id
extern unsigned int DAT_80612128; // currently-playing sound handle
extern int   DAT_80612118;    // pending priority-queue count (cap 4)
extern int   DAT_80588c78;    // priority-queue handles (stride 8)
extern int   DAT_80588c7c;    // priority-queue priorities (stride 8)
extern int   DAT_80612110;    // gate flag
extern int   DAT_80611934;    // sub-mode / phase
extern int   DAT_80611928;    // current game frame counter
extern int   DAT_8061211c;    // last-combat-bark frame stamp
extern int   DAT_806109c0;    // frames-per-tick / rate divisor
extern int   DAT_806108e4;    // scheduler-enabled flag
extern char  DAT_805015bd;    // score/arcade flag ('\n')
extern int   DAT_805015c0;    // global option bits (0x40 = barks disabled)
extern int   DAT_80611c24;    // trigger/actor pool base (0x2e8 stride)

// Bark global sound-handle slots (shared with aiactor_bark.cpp).
extern unsigned int DAT_80612138;
extern unsigned int DAT_80612140;

// Timing / volume floats.
extern float FLOAT_8061214c;  // talk cooldown timer
extern float FLOAT_80612148;  // secondary cooldown timer
extern float FLOAT_80612130;  // bark quiet volume (per mode)
extern float FLOAT_80612134;  // bark normal volume (per mode)
extern float FLOAT_806108ec;  // per-frame timer decrement scale
extern float FLOAT_80615e4c;  // 0.0
extern float FLOAT_80615e50;  // 1.0 / default duration
extern float FLOAT_80615e60;  // secondary cooldown reset value
extern float FLOAT_80615e64;
extern float FLOAT_80615e68;
extern float FLOAT_80615e6c;
extern float FLOAT_80615e70;  // secondary re-arm delay
extern float FLOAT_80615e80;  // random-index bias
extern float FLOAT_80615e84;  // random-index scale
extern double DOUBLE_80615e78; // int->double magic bias (0x4330000000000000)
extern double DOUBLE_80615e88; // int->double magic bias for rng

// s32 -> float via the Gekko magic-double trick, matching the decompiler.
static inline float i2f_bias(int v)
{
    return (float)((double)CONCAT44(0x43300000, (unsigned int)v ^ 0x80000000) - DOUBLE_80615e78);
}
static inline float u2f_rng(unsigned int v)
{
    return (float)((double)CONCAT44(0x43300000, v) - DOUBLE_80615e88);
}

// ---------------------------------------------------------------------------
// aiactor_bark_release_samples (0x801432dc)
static inline void release_table(int count, const int* table)
{
    for (int i = 0; i < count; ++i)
        sound_clear_bank_ref(sound_resolve_sample_id(table[i]));
}
void aiactor_bark_release_samples(void)
{
    __aiactor_frame_begin();
    switch (DAT_80612614)
    {
    case 1:    release_table(DAT_8040de40, &DAT_8040de44); break;
    case 2:    release_table(DAT_8040dbd8, &DAT_8040dbdc); break;
    case 4:    release_table(DAT_8040dc04, &DAT_8040dc08); break;
    case 5:    release_table(DAT_8040dd0c, &DAT_8040dd10);
               release_table(DAT_8040dce0, &DAT_8040dce4); break;
    case 6:    release_table(DAT_8040dcb4, &DAT_8040dcb8); break;
    case 9:    release_table(DAT_8040ddbc, &DAT_8040ddc0); break;
    case 0xb:  release_table(DAT_8040dc5c, &DAT_8040dc60);
               release_table(DAT_8040dc88, &DAT_8040dc8c); break;
    case 0xe:  release_table(DAT_8040dd90, &DAT_8040dd94); break;
    case 0x11: release_table(DAT_8040de6c, &DAT_8040de70); break;
    case 0x12: release_table(DAT_8040dc30, &DAT_8040dc34); break;
    case 0x16: release_table(DAT_8040de98, &DAT_8040de9c); break;
    case 0x1a: release_table(DAT_8040dd38, &DAT_8040dd3c);
               release_table(DAT_8040dd64, &DAT_8040dd68); break;
    case 0x51: release_table(DAT_8040dde8, &DAT_8040ddec);
               release_table(DAT_8040de14, &DAT_8040de18); break;
    default: break;
    }
    __aiactor_frame_end();
}

// ---------------------------------------------------------------------------
// aiactor_bark_request_talk (0x80143754)
// If the actor has a bark def for the current mode, latch its handle (+0x2c)
// as the pending "talk" request and reset the talk cooldown timer.
void aiactor_bark_request_talk(int actor)
{
    int* def = aiactor_get_bark_def_for_mode(actor);
    if (def != (int*)0)
    {
        DAT_80612154  = *(int*)(actor + 0x2c);
        FLOAT_8061214c = FLOAT_80615e50;
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_death_line_for_mode (0x80143798)
// Mode -> death/secondary bark line id.
int aiactor_bark_death_line_for_mode(void)
{
    if (DAT_80612614 != 5)
    {
        if (DAT_80612614 < 5)
        {
            if (3 < DAT_80612614)
                return 0x1ac5;   // mode 4
        }
        else if (DAT_80612614 < 7)
        {
            return 0x108d;       // mode 6
        }
    }
    else
    {
        return 0x108d;           // mode 5
    }
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_bark_request_secondary (0x801437d8)
void aiactor_bark_request_secondary(int actor)
{
    int line = aiactor_bark_death_line_for_mode();
    if ((line != 0) && (FLOAT_80612148 <= FLOAT_80615e4c))
    {
        DAT_80612150   = *(int*)(actor + 0x2c);
        FLOAT_80612148 = FLOAT_80615e60;
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_reset_state (0x8014382c)
void aiactor_bark_reset_state(void)
{
    DAT_80612154 = 0;
    DAT_80612150 = 0;
    memset_wrapper((int)&DAT_80612138 - 8, 0, 8);   // -0x7f9edec8 == 0x80612130 region
    DAT_80612138 = 0xffffffff;
    memset_wrapper((int)&DAT_80612140 - 8, 0, 8);   // -0x7f9edec0 == 0x80612138 region
    DAT_80612140 = 0xffffffff;
    if (DAT_80612614 == 4)
    {
        FLOAT_80612134 = FLOAT_80615e64;
        FLOAT_80612130 = FLOAT_80615e68;
    }
    else
    {
        FLOAT_80612134 = FLOAT_80615e6c;
        FLOAT_80612130 = FLOAT_80615e64;
    }
}

// resolve a queued actor handle to its pool pointer (generation-checked), or 0.
static int resolve_actor_handle(int* slot)
{
    int handle = *slot;
    if (handle == 0)
        return 0;
    int obj = DAT_80611c24 + ((unsigned int)handle >> 0x16) * 0x2e8;
    if (*(unsigned int*)(obj + 0x2c) != (unsigned int)handle)
    {
        *slot = 0;
        return 0;
    }
    return obj;
}

// ---------------------------------------------------------------------------
// aiactor_bark_scheduler_tick (0x801438b8)
void aiactor_bark_scheduler_tick(void)
{
    if (DAT_806108e4 == 0)
        return;

    // --- pending "talk" bark ---
    if (DAT_80612154 != 0)
    {
        FLOAT_8061214c = -(FLOAT_806108ec * (FLOAT_80615e50 / i2f_bias(DAT_806109c0)) - FLOAT_8061214c);
        if (FLOAT_8061214c <= FLOAT_80615e4c)
        {
            int obj = resolve_actor_handle(&DAT_80612154);
            if (obj != 0)
            {
                int self = *(int*)(obj + 0x7c);
                int* def = aiactor_get_bark_def_for_mode(obj);
                unsigned int r = __rng_u32();
                unsigned int count = (unsigned int)def[0];
                unsigned int line = (unsigned int)def[(r - (r / count) * count) + 1];
                if ((line == 0xfad) || (line == 0x1ac4))
                    aiactor_play_bark_global((int*)line, 0x100, (int*)0, 0, 0);
                else
                    aiactor_play_bark_3d((int*)self, (int)line, 0x900, (int*)0, 0, 0, 0, 0);
            }
            DAT_80612154 = 0;
        }
    }

    // --- secondary cooldown decay ---
    if (FLOAT_80615e4c < FLOAT_80612148)
    {
        FLOAT_80612148 = -(FLOAT_806108ec * (FLOAT_80615e50 / i2f_bias(DAT_806109c0)) - FLOAT_80612148);
    }

    // --- pending "secondary/death" bark ---
    if (DAT_80612150 != 0)
    {
        if (FLOAT_80612148 <= FLOAT_80615e4c)
        {
            int obj = resolve_actor_handle(&DAT_80612150);
            if (obj != 0)
            {
                int line = aiactor_bark_death_line_for_mode();
                aiactor_play_bark_3d((int*)*(int*)(obj + 0x7c), line, 0x100, (int*)0, 0, 0, 0, 0);
                DAT_80612150   = 0;
                FLOAT_80612148 = FLOAT_80615e70;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_select_event (0x80143c50)
// Arbitrate a candidate event id against the currently-playing bark + the
// event priority table (&DAT_8040e158, stride 0xc) + a cooldown gate; commit
// the winner to DAT_80612120. Faithful non-matching translation of the
// original nested-condition arbitration.
void aiactor_bark_select_event(int eventId)
{
    int selected = DAT_80612120;

    // Table stride is 0xc bytes; index into the byte-addressed base symbol.
    int candField  = *(int*)((int)&DAT_8040e158 + eventId * 0xc);
    int candWeight = (&DAT_8040e15c)[eventId * 3];

    if (((DAT_805015bd != '\n') || ((DAT_805015c0 & 0x40) != 0)) &&
        (-1 < eventId) && (eventId < 0x37) &&
        ((DAT_80611934 == 5) || (DAT_80612110 == 0)) &&
        (DAT_80612118 < 1) &&
        ((0x31 < candField) ||
         (FLOAT_80615e6c * i2f_bias(DAT_806109c0) <= i2f_bias(DAT_80611928 - candWeight))))
    {
        int playing = sound_is_playing(DAT_80612128);
        if ((playing == 0) ||
            ((DAT_80612124 < 0) || (0x36 < DAT_80612124) ||
             (*(int*)((int)&DAT_8040e158 + DAT_80612124 * 0xc) <= candField)))
        {
            if ((-1 < DAT_80612120) && (DAT_80612120 < 0x37))
            {
                int curField = *(int*)((int)&DAT_8040e158 + DAT_80612120 * 0xc);
                if (candField < curField)
                    return;
                if ((candField < 0x32) && (curField == candField))
                {
                    unsigned int r = __rng_u32();
                    if ((int)(FLOAT_80615e80 + u2f_rng(r) / FLOAT_80615e84) == 0)
                        return;
                }
                selected = eventId;
            }
        }
    }
    DAT_80612120 = selected;
}

// ---------------------------------------------------------------------------
// aiactor_bark_pick_random_event (0x80143ab4)
// Roll a random event id in [lo,hi]; pick the lowest-weight eligible entry
// (bailing at the first entry whose weight exceeds the rolled entry's) and
// forward it to aiactor_bark_select_event.
void aiactor_bark_pick_random_event(int lo, int hi)
{
    unsigned int r = __rng_u32();
    int best = 0x7fffffff;
    int chosen = -1;

    int rolled = lo + (int)(i2f_bias(hi - lo) * (u2f_rng(r) / FLOAT_80615e84) + FLOAT_80615e80);
    if ((-1 < rolled) && (rolled < 0x37))
    {
        int idx = lo;
        int weightOff = lo * 0xc;
        int result = chosen;
        int remaining = (hi + 1) - lo;
        if (lo <= hi)
        {
            do
            {
                if (idx != rolled)
                {
                    int w = *(int*)((int)&DAT_8040e15c + weightOff);
                    result = rolled;
                    if ((&DAT_8040e15c)[rolled * 3] < w)
                        break;
                    if (w < best)
                    {
                        chosen = idx;
                        best = w;
                    }
                }
                ++idx;
                weightOff += 0xc;
                --remaining;
                result = chosen;
            } while (remaining != 0);
        }
        if (result != -1)
            aiactor_bark_select_event(result);
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_queue_insert (0x80143bd4)
// Insert a (handle,priority) pair into the 4-slot pending queue, keeping it in
// descending priority order and bailing early if a priority < 10 is met.
void aiactor_bark_queue_insert(int handle, int priority)
{
    int idx = 0;
    int off = 0;
    int slot = DAT_80612118;
    int remaining = DAT_80612118;
    if (0 < DAT_80612118)
    {
        do
        {
            slot = idx;
            if (*(int*)((int)&DAT_80588c7c + off) < priority)
                break;
            if (*(int*)((int)&DAT_80588c7c + off) < 10)
                return;
            ++idx;
            off += 8;
            --remaining;
            slot = DAT_80612118;
        } while (remaining != 0);
    }
    DAT_80612118 = slot;
    int at = DAT_80612118;
    if (DAT_80612118 < 4)
    {
        (&DAT_80588c7c)[DAT_80612118 * 2] = priority;
        DAT_80612118 = DAT_80612118 + 1;
        (&DAT_80588c78)[at * 2] = handle;
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_combat_triggers (0x80143e10)
// Per-frame combat-relationship bark driver (from character_on_death_handler).
// Reads an actor pair from the packed gpr frame, and based on their combat
// state and relationship fires taunt/reaction/kill barks.
void aiactor_bark_combat_triggers(void)
{
    int* frame = (int*)__aiactor_frame_begin();

    int aEnt = frame[0];
    if (((DAT_805015c0 & 0x40) != 0) || (aEnt == 0) || (*(int*)(aEnt + 0x20) != 8))
        goto done;
    {
        int* selfA = *(int**)(aEnt + 0x7c);
        int bEnt = frame[1];
        if ((bEnt == 0) || (*(int*)(bEnt + 0x20) != 8) ||
            (DAT_80611928 - DAT_8061211c < DAT_806109c0 << 3))
            goto done;
        int* selfB = *(int**)(bEnt + 0x7c);

        // A alive-and-visible?
        if ((*(int*)(selfA[0] + 0x150) != 0) &&
            (-1 < *(int*)(*(int*)(selfA[0] + 0x150) + 4)))
        {
            if (selfB == selfA)
            {
                aiactor_bark_pick_random_event(0x26, 0x27);
                goto done;
            }
            aiactor_bark_pick_random_event(0x2f, 0x30);
        }

        // B alive-and-visible?
        if ((*(int*)(selfB[0] + 0x150) != 0) &&
            (-1 < *(int*)(*(int*)(selfB[0] + 0x150) + 4)))
        {
            if (player_ai_is_story_mode((int)*(short*)((int)selfA + 0x48)) != 0)
                aiactor_bark_select_event(0x35);

            int st = frame[9];
            bool fireTaunt = false;
            if (st < 0x18)
            {
                if (st < 1)      { if (st < -1) fireTaunt = true; }
                else if (st < 0x15) fireTaunt = true;
            }
            else if (st != 0x1e) fireTaunt = true;
            if (fireTaunt)
            {
                if ((frame[0x11] < 5) && (2 < frame[0x11]))
                    aiactor_bark_pick_random_event(0x1f, 0x22);
            }
        }

        // either alive-and-visible: reaction barks
        if (((*(int*)(selfB[0] + 0x150) != 0) && (-1 < *(int*)(*(int*)(selfB[0] + 0x150) + 4))) ||
            ((*(int*)(selfA[0] + 0x150) != 0) && (-1 < *(int*)(*(int*)(selfA[0] + 0x150) + 4))))
        {
            if ((frame[0x11] < 2) && (-1 < frame[0x11]))
                aiactor_bark_pick_random_event(0x31, 0x33);
            int weap = frame[5];
            if (weap == 0x21)
                aiactor_bark_select_event(0x28);
            else if ((weap < 0x21) && (weap == 0))
                aiactor_bark_select_event(0x23);
            if (frame[9] == 0x15)
                aiactor_bark_pick_random_event(0x2c, 0x2d);
            if ((frame[0xf] & 1) != 0)
                aiactor_bark_select_event(0x29);
        }
    }
done:
    __aiactor_frame_end();
}


// ===========================================================================
// Bark playback dispatcher + system setup (batch_408, 0x8014403c-0x801444b0).
// These finish the scheduler: is-active query, the per-frame playback engine
// that actually starts/fades bark sounds through the priority queue, the
// localized-sound resolver, and one-time system setup/reset.
// ===========================================================================

extern "C" {
extern unsigned int FUN_80364098(void);  // system-language/localization variant (0..4)
extern int          FUN_8022d778(void);   // arcade sub-state gate predicate
}

extern int   DAT_80612114;   // last dequeued queue priority (throttle stamp)
extern int   DAT_80588c80;   // priority-queue handle slot[1] base (for shift)
extern int   DAT_80588c84;   // priority-queue priority slot[1] base (for shift)
extern int   DAT_80611000;   // level/actor-loaded flag (CLZ==0 test source)
extern int   DAT_8040e154;   // event->sound-id field (priority table +(-4) i.e. [k].-0x04)
extern int   DAT_8040e184;   // localized bark sound id A
extern int   DAT_8040e190;   // localized bark sound id B
extern int   DAT_8040e19c;   // localized bark sound id C
extern int   DAT_8040e1a8;   // localized bark sound id D
extern float FLOAT_80615e90;
extern float FLOAT_80615e94;
extern float FLOAT_8061212c;   // per-language bark playback rate / duration

// ---------------------------------------------------------------------------
// aiactor_bark_is_active (0x8014403c)
// True when a bark is currently playing (handle DAT_80612128 >= 0) or the
// pending queue is non-empty.
unsigned int aiactor_bark_is_active(void)
{
    if (((int)DAT_80612128 < 0) && (DAT_80612118 == 0))
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// aiactor_bark_playback_tick (0x80144060)
// Per-frame bark playback engine. When barks are enabled (score flag / option
// bit), either service the currently-selected single event (DAT_80612120) or,
// if the priority queue holds entries, fade the current line and start the
// next queued one via sound_play_request, then shift the queue down.
void aiactor_bark_playback_tick(void)
{
    int selected = DAT_80612120;
    if ((DAT_805015bd != '\n') || ((DAT_805015c0 & 0x40) != 0))
    {
        if (DAT_80612118 < 1)
        {
            if ((DAT_80612120 < 0) || (0x36 < DAT_80612120))
            {
                if (sound_is_playing(DAT_80612128) == 0)
                {
                    DAT_80612128 = 0xffffffff;
                    DAT_80612124 = -1;
                }
            }
            else
            {
                int fieldOff = DAT_80612120 * 0xc;
                if (sound_is_playing(DAT_80612128) != 0)
                {
                    if (*(int*)((int)&DAT_8040e158 + fieldOff) < 0x32)
                    {
                        DAT_80612120 = 0xffffffff;
                        return;
                    }
                    float rate = (float)((double)FLOAT_80615e50 / (double)i2f_bias(DAT_806109c0));
                    sound_register_fade_out((double)FLOAT_80615e50, (double)rate,
                                            (int*)&DAT_80612128, (unsigned int*)0);
                }
                float dur = FLOAT_8061212c;
                (&DAT_8040e15c)[selected * 3] = DAT_80611928;
                int soundId = *(int*)((int)&DAT_8040e154 + fieldOff);
                int params[10];
                sound_init_play_params((int)params);
                ((unsigned int*)params)[5] |= 0x200;   // local_78 |= 0x200
                ((float*)params)[7] = dur;              // local_70
                params[0] = soundId;
                DAT_80612128 = sound_play_request(params);
                DAT_80612124 = DAT_80612120;
                DAT_80612120 = -1;
            }
        }
        else
        {
            if ((DAT_80612124 != 0x36) || (DAT_80612114 < DAT_80588c7c))
            {
                float rate = (float)((double)FLOAT_80615e50 / (double)i2f_bias(DAT_806109c0));
                sound_register_fade_out((double)FLOAT_80615e50, (double)rate,
                                        (int*)&DAT_80612128, (unsigned int*)0);
                DAT_80612124 = -1;
                DAT_80612128 = 0xffffffff;
            }
            int params[10];
            params[0] = DAT_80588c78;
            if (sound_is_playing(DAT_80612128) == 0)
            {
                float dur = FLOAT_8061212c;
                sound_init_play_params((int)params);
                ((unsigned int*)params)[5] |= 0x200;
                ((float*)params)[7] = dur;
                DAT_80612128 = sound_play_request(params);
                DAT_80612114 = DAT_80588c7c;
                DAT_80612124 = 0x36;
                // shift the queue down by one (count-1 entries, stride 8).
                int n = DAT_80612118 - 1;
                int off = 0;
                if (0 < n)
                {
                    do
                    {
                        *(int*)((int)&DAT_80588c78 + off) = *(int*)((int)&DAT_80588c80 + off);
                        *(int*)((int)&DAT_80588c7c + off) = *(int*)((int)&DAT_80588c84 + off);
                        --n;
                        off += 8;
                    } while (n != 0);
                }
                DAT_80612118 = DAT_80612118 - 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_resolve_localized_sound (0x801442ac)
// Resolve a base bark sound id to its localized variant using the system
// language index (FUN_80364098: 1=-4, 2=-3, 3=-2, 4=-1, other=0 offset from
// baseId), store it in *out, and drop its bank reference.
void aiactor_bark_resolve_localized_sound(int* out, int baseId)
{
    unsigned int lang = FUN_80364098();
    if (lang == 3)          *out = baseId - 2;
    else if ((int)lang < 3)
    {
        if (lang == 1)      *out = baseId - 4;
        else if (0 < (int)lang) *out = baseId - 3;
        else                *out = baseId;
    }
    else if ((int)lang < 5) *out = baseId - 1;
    else                    *out = baseId;
    sound_clear_bank_ref(*out);
}

// ---------------------------------------------------------------------------
// aiactor_bark_playback_reset (0x801444b0)
// Reset the playback engine state and clear all 0x37 event-weight stamps to
// -0x3e8 (0xfffffc18) so every event is immediately eligible.
void aiactor_bark_playback_reset(void)
{
    DAT_80612128 = 0xffffffff;
    DAT_80612124 = 0xffffffff;
    DAT_80612120 = 0xffffffff;
    DAT_80612118 = 0;
    DAT_80612114 = 0;
    int off = 0;
    for (int i = 0x37; i != 0; --i)
    {
        *(int*)((int)&DAT_8040e15c + off) = (int)0xfffffc18;
        off += 0xc;
    }
}

// ---------------------------------------------------------------------------
// aiactor_bark_system_setup (0x80144348)
// One-time bark system init: compute the enable gate DAT_80612110 (from option
// bits + level-loaded flag), the per-language playback rate FLOAT_8061212c,
// resolve the four localized event sounds, and reset playback state.
void aiactor_bark_system_setup(void)
{
    if (DAT_80612614 < 0x65)
    {
        bool doResolve = true;
        if ((DAT_805015bd == '\n') && ((doResolve = false), (DAT_805015c0 & 0x40) != 0) &&
            (FUN_8022d778() != 0))
        {
            doResolve = true;
        }
        if (doResolve)
        {
            aiactor_bark_resolve_localized_sound(&DAT_8040e184, 0x162e);
            aiactor_bark_resolve_localized_sound(&DAT_8040e190, 0x1633);
            aiactor_bark_resolve_localized_sound(&DAT_8040e19c, 0x1638);
            aiactor_bark_resolve_localized_sound(&DAT_8040e1a8, 0x163d);
        }
        if ((DAT_805015c0 & 0x40) == 0)
        {
            if ((DAT_805015c0 & 0x8000000) == 0)
            {
                if ((DAT_805015c0 & 0x4000000) == 0)
                    DAT_80612110 = (DAT_80611000 == 0) ? 1 : 0;   // CLZ(x)>>5
                else
                    DAT_80612110 = 1;
            }
            else
            {
                DAT_80612110 = 0;
            }
        }
        else
        {
            DAT_80612110 = 1;
        }
    }
    else
    {
        DAT_80612110 = 1;
    }

    unsigned int lang = FUN_80364098();
    if (lang == 4)
        FLOAT_8061212c = FLOAT_80615e90;
    else if (((int)lang < 4) && (lang == 0))
        FLOAT_8061212c = FLOAT_80615e50;
    else
        FLOAT_8061212c = FLOAT_80615e94;

    aiactor_bark_playback_reset();
}
