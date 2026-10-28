// Address range: 0x80142a20 - 0x801430c0 | batch_406 | 2026-09-05
//
// AI actor bark / spoken-line (announcer + subtitle) subsystem, plus a couple
// of respawn/owner helpers that live in the same address neighbourhood as the
// target-scan cluster (aiactor_target_scan.cpp, batch_405).
//
// A "bark" is a positional voice line played by an AI actor (or the mission
// announcer) together with an optional on-screen subtitle. The play helpers
// resolve a sample id, play it as a 3D source, stash the returned sound handle
// on the actor/global slot, optionally set a pitch, and create a per-player
// multiline subtitle while the round timer is running. The exact set of lines
// available depends on the current game mode (DAT_80612614), which selects an
// entry in the bark-definition table &DAT_8040dbd8 (stride 0xb ints).
//
// Shared data:
//   * DAT_80612614  - current game mode id.
//   * DAT_80612610  - base player count (own-team block; also coop count base).
//   * DAT_806120fc  - coop-partition actor count.
//   * DAT_80612098  - active actor count (total).
//   * DAT_80611d90  - coop actor pool base (stride 0xea0).
//   * DAT_80588610  - active actor pointer list base.
//   * DAT_805015bd  - score-mode / arcade flag ('\n' => score/arcade path).
//   * DAT_805015c0  - global option bits (0x40 = disable bark model stream).
//   * DAT_80612130/34 & 4c/50/54/58/5c - bark volume / 3D-position params.
//   * DAT_80612138/40 - global bark sound-handle slots (+handle, +text owner).
//   * DAT_8040dbd8  - bark-definition table (stride 0xb ints), indexed per mode.
//   * DAT_806125e8 / DAT_805c9858 / DAT_8048ca80 - weapon/char model tables.
//
// Per actor (param_1/actor): +0x7c -> render/AI object; on that object
//   +0xbf0 -> alert/dialogue object (whose +8 is a 4CC dialogue-set tag);
//   +0xe90 bark sound handle, +0xe94 zone-door history list head, +0x3a4 (word
//   index into the object) the last-played handle.

#include "common.h"

// ---- raw / named cross-module helpers (self-externed; not in shared decls) --
extern "C" {
// Sound + subtitle helpers (named in the IDB, defined elsewhere/not yet in src).
extern int  sound_resolve_sample_id(int id);
extern int  sound_set_reverb(int sampleId);
extern int  sound_play_or_resume_3d(double vol, double a, double b, int sampleId,
                                     int worldPos, int loop);
extern void sound_set_pitch(double pitch, int handle);
extern void sound_stop_handle(void);
extern int  game_get_round_timer(void);
extern void hud_clear_text_by_owner(void);
extern int  hud_create_multiline_text(double a, double b, double c, double d,
                                       double e, double f, double g, double h,
                                       int player, int textId, int type,
                                       int owner, int arg, int handle,
                                       int p7, int p8);
extern int  get_string_by_id(int id);

// Coop owner-death dispatch (wide float ABI, defined in damage system).
extern int  character_on_death_init(int f1, double a, double b, double c,
                                     int p5, int p6, int p7, int p8, int actor);

// Spawn/model streaming helpers.
extern int  level_get_default_weapon(unsigned int weaponId, int* outSlot, int* outAux);
extern int  level_get_current_id(void);
extern int  level_actor_get_load_status(int levelId);
extern char* path_get_filename(char* path);
extern int  FUN_80387fb8(int f1, double a, double b, double c, int p5, int p6,
                         int p7, int p8, int outBuf, int dirStr, int nameA,
                         char* nameB, int table, int r8, int r9, int r10);

// Zone-door / dialogue-object helpers.
extern void FUN_801c007c(unsigned int* listHead);   // clear intrusive list
extern int  FUN_8027eb3c(int aiObject);              // dialogue-gate predicate
extern int  FUN_80364290(int aiObject, int mode);    // objective-owner predicate
extern int  FUN_80220f30(int flagId);                // game-flag query
extern void FUN_801bff9c(int* aiObject, int str, int handle); // set subtitle text

// Packed gpr save/restore runtime helpers (Ghidra models a 64-bit return that
// aliases the two incoming argument registers; see notes on each caller).
extern unsigned long long __save_gpr_28(void);
extern void               __restore_gpr_28(void);
extern unsigned long long __save_gpr_25(void);
extern void               __restore_gpr_25(void);
}

// ---- raw data symbols (per-file externs, typed to their use here) ----------
extern int   DAT_80612614;   // current game mode id
extern int   DAT_80612610;   // base player count / coop base
extern int   DAT_806120fc;   // coop-partition actor count
extern int   DAT_80612098;   // active actor count
extern int   DAT_80611d90;   // coop actor pool base
extern int   DAT_80588610;   // active actor pointer list base
extern char  DAT_805015bd;   // score/arcade flag ('\n')
extern int   DAT_805015c0;   // global option bits
extern int   DAT_806125e8;   // char/model descriptor table base
extern short DAT_805c9858;   // model index table (shorts)
extern int   DAT_8048ca80;   // weapon slot table base
extern int   DAT_8040dbd8;   // bark-definition table (stride 0xb)
extern unsigned int DAT_80612138; // global bark slot A: [0]=handle,[1]=owner
extern unsigned int DAT_80612140; // global bark slot B
extern float FLOAT_80612130;
extern float FLOAT_80612134;
extern float FLOAT_80615e4c;
extern float FLOAT_80615e50;
extern float FLOAT_80615e54;
extern float FLOAT_80615e58;
extern int   s_assets_ts_models__s_gamecube_out_8040dba8; // format string

// ---------------------------------------------------------------------------
// aiactor_find_bot_by_linked_entity (0x80142a20)
// Scan the bots partition (slots [DAT_80612610, DAT_80612098)) of the active
// actor list for a bot that is not gated-out (list entry +9 bit 8 clear) and
// whose linked entity (list entry +0x2fc -> +8) matches entityId. Returns the
// matching bot's id (entry[0]) or 0 if none.
unsigned int aiactor_find_bot_by_linked_entity(int entityId)
{
    int srcOff = DAT_80612610 << 2;
    int remaining = DAT_80612098 - DAT_80612610;
    if (DAT_80612610 < DAT_80612098)
    {
        do
        {
            unsigned int* e = *(unsigned int**)((int)&DAT_80588610 + srcOff);
            if ((e[9] & 8) == 0)
            {
                if (e[0x2fc] != 0)
                {
                    if (*(int*)(e[0x2fc] + 8) == entityId)
                        return e[0];
                }
            }
            srcOff += 4;
            --remaining;
        } while (remaining != 0);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_dispatch_owner_death (0x80142a84)
// Walk the coop actor pool (DAT_80611d90, stride 0xea0, DAT_806120fc entries
// starting at DAT_80612610) and, for every coop actor whose team/state slot is
// non-zero (fields differ by score-mode flag) and whose linked entity matches
// the incoming owner-entity id, invoke character_on_death_init.
//
// The owner-entity id and the pass-through float/int args arrive in the packed
// save-register frame that Ghidra models as __save_gpr_28(); here we surface
// the id as ownerEntity and thread the float args (fa/fb/fc + p5..p8).
void aiactor_dispatch_owner_death(int ownerEntity, double fa, double fb, double fc,
                                  int p5, int p6, int p7, int p8)
{
    (void)__save_gpr_28();
    int poolOff = DAT_80612610 * 0xea0;
    int f1 = 0;
    for (int i = DAT_80612610; i < DAT_806120fc + DAT_80612610; ++i)
    {
        int* actor = (int*)(DAT_80611d90 + poolOff);
        int hiField, loField;
        if (DAT_805015bd == '\n')
        {
            loField = actor[4];
            hiField = actor[5];
        }
        else
        {
            loField = actor[6];
            hiField = actor[7];
        }
        if ((hiField != 0 || loField != 0) &&
            (*(int*)(actor[0x2fc] + 8) == ownerEntity))
        {
            f1 = character_on_death_init(f1, fa, fb, fc, p5, p6, p7, p8, actor[0]);
        }
        poolOff += 0xea0;
    }
    __restore_gpr_28();
}

// ---------------------------------------------------------------------------
// aiactor_bark_channel_for_mode (0x80142b38)
// Bark/announcer sound-channel selector: mode 4 uses channel 7, everything
// else channel 8.
unsigned int aiactor_bark_channel_for_mode(void)
{
    if (DAT_80612614 != 4)
        return 8;
    return 7;
}

// ---------------------------------------------------------------------------
// aiactor_stream_spawn_char_model (0x80142b58)
// For a spawn record (spawnRec), resolve its default weapon/character, and if
// the current mode + level state requires it, kick off an asynchronous stream
// of the character model via FUN_80387fb8. Returns 1 when nothing needed to be
// streamed (ready), 0 when a stream was queued.
unsigned int aiactor_stream_spawn_char_model(int spawnRec)
{
    int  localSlot = 0;
    int  auxSlot = 0;
    int  scratch[50];   // auStack_d0 [200 bytes] scratch for the async request
    int  in_r8 = 0, in_r9 = 0, in_r10 = 0;

    int hdr = level_get_default_weapon((unsigned int)*(unsigned short*)(spawnRec + 0x16),
                                       &localSlot, &auxSlot);
    int weaponIdx = *(int*)((int)&DAT_8048ca80 + localSlot * 0x30) * 2;

    unsigned int needStream;
    if ((DAT_805015c0 & 0x40) == 0)
    {
        if ((DAT_80612614 == 100) || (DAT_80612614 == 0x38))
        {
            needStream = 0;
        }
        else if ((DAT_805015bd == '\n') || (DAT_80612614 == 0x66))
        {
            needStream = *(unsigned int*)(DAT_806125e8 +
                             *(short*)((int)&DAT_805c9858 + weaponIdx) * 0x30 + 0x28) & 0x2000000;
        }
        else
        {
            needStream = 0;
        }
    }
    else
    {
        needStream = 0;
    }

    if (needStream == 0)
        return 1;

    int table = DAT_806125e8;
    int levelId = level_get_current_id();
    if (level_actor_get_load_status(levelId) == 0)
        return 1;

    char* fname = path_get_filename(
        *(char**)(DAT_806125e8 + *(short*)((int)&DAT_805c9858 + weaponIdx) * 0x30 + 0x10));
    FUN_80387fb8(hdr, 0.0, 0.0, 0.0, 0, 0, 0, 0, (int)scratch,
                 (int)&s_assets_ts_models__s_gamecube_out_8040dba8,
                 *(int*)(DAT_806125e8 + *(short*)((int)&DAT_805c9858 + weaponIdx) * 0x30 + 0x10),
                 fname, table, in_r8, in_r9, in_r10);
    return 0;
}

// ---------------------------------------------------------------------------
// aiactor_stop_bark_slot (0x80142c94)
// If the slot holds a live sound handle (>= 0), stop the sound, invalidate the
// handle, and clear the associated on-screen subtitle.
void aiactor_stop_bark_slot(int* slot)
{
    if (-1 < *slot)
    {
        sound_stop_handle();
        *slot = -1;
        hud_clear_text_by_owner();
    }
}

// ---------------------------------------------------------------------------
// aiactor_clear_bark_state (0x80142cdc)
// Clear an actor's zone-door history list (aiObject +0xe94) and stop its active
// bark (aiObject +0xe90) + subtitle. actor +0x7c -> aiObject.
void aiactor_clear_bark_state(int actor)
{
    int aiObject = *(int*)(actor + 0x7c);
    FUN_801c007c((unsigned int*)(aiObject + 0xe94));
    if (-1 < *(int*)(aiObject + 0xe90))
    {
        sound_stop_handle();
        *(int*)(aiObject + 0xe90) = -1;
        hud_clear_text_by_owner();
    }
}

// ---------------------------------------------------------------------------
// aiactor_play_bark_3d (0x80142d2c)
// Play a positional bark line on an actor and show the per-player subtitle.
//
//   self     - AI/dialogue object (Ghidra: high word of __save_gpr_25 frame)
//   soundId  - line/string id       (Ghidra: low word of the frame)
//   flags    - bit 0x800 = quiet volume, 0x4000 = non-positional (fixed pos),
//              0x10000 = apply pitch.
//   barkRec  - optional per-line record: [8]=stored handle, [9]=source obj id,
//              [0]->+0x38 completion callback, [0]->+0x1c subtitle owner.
//
// Skips entirely when the dialogue object's mode word (self +5/+7 by score
// flag) has bit 0 set (bark suppressed for this mode).
void aiactor_play_bark_3d(int* self, int soundId, unsigned int flags, int* barkRec,
                          int p5, int p6, int p7, int p8)
{
    (void)__save_gpr_25();

    int recBase = (barkRec == (int*)0) ? 0 : barkRec[0];
    int sampleId = sound_resolve_sample_id(soundId);

    unsigned int modeWord = (DAT_805015bd == '\n') ? (unsigned int)self[5]
                                                    : (unsigned int)self[7];
    if ((modeWord & 1) == 0)
    {
        if (-1 < sampleId)
        {
            float vol = (flags & 0x800) ? FLOAT_80612130 : FLOAT_80612134;
            aiactor_clear_bark_state(self[0]);
            double reverb = (double)sound_set_reverb(sampleId);
            unsigned int handle;
            if ((flags & 0x4000) == 0)
            {
                handle = (unsigned int)sound_play_or_resume_3d(
                    (double)vol, (double)FLOAT_80615e50, (double)FLOAT_80615e58,
                    sampleId, self[0] + 100, 1);
            }
            else
            {
                handle = (unsigned int)sound_play_or_resume_3d(
                    (double)FLOAT_80615e4c, (double)FLOAT_80615e50, (double)FLOAT_80615e54,
                    sampleId, 0, 1);
            }
            if (barkRec != (int*)0)
                barkRec[9] = *(int*)(self[0] + 0x2c);
            if ((flags & 0x10000) != 0)
                sound_set_pitch((double)FLOAT_80615e4c, (int)handle);
            if (barkRec != (int*)0)
                barkRec[8] = (int)handle;
            self[0x3a4] = (int)handle;

            int timer = game_get_round_timer();
            if ((timer != 0) && (soundId != 0))
            {
                int owner = (barkRec == (int*)0) ? 0 : *(int*)(barkRec[0] + 0x1c);
                for (int player = 0; player < DAT_80612610; ++player)
                {
                    hud_create_multiline_text(
                        (double)FLOAT_80615e4c, reverb, (double)FLOAT_80615e54,
                        0.0, 0.0, 0.0, 0.0, 0.0,
                        player, self[2], 9, soundId, owner, (int)handle, p7, p8);
                }
            }
        }
        int str = get_string_by_id(soundId);
        FUN_801bff9c(self, str, self[0x3a4]);
        if ((recBase != 0) && (*(void**)(recBase + 0x38) != (void*)0))
            (*(void(*)(int*))*(void**)(recBase + 0x38))(barkRec);
    }
    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// aiactor_play_bark_global (0x80142f48)
// Like aiactor_play_bark_3d but plays through one of two global (non-actor)
// bark slots (DAT_80612138 / DAT_80612140, chosen by frame bit 0x40000000).
//   self    - dialogue object (high word of __save_gpr_25 frame)
//   soundId - line id         (low word of the frame; also carries the mode
//             selector bits 0x200 quiet / 0x10000 pitch)
void aiactor_play_bark_global(int* self, unsigned int frameFlags, int* barkRec,
                              int p9, int p10)
{
    (void)__save_gpr_25();
    int soundId = (int)self;          // low word aliases the line id in the frame
    int recBase = (barkRec == (int*)0) ? 0 : barkRec[0];
    int sampleId = sound_resolve_sample_id(soundId);

    unsigned int* slot = &DAT_80612140;
    if ((frameFlags & 0x40000000) != 0)
        slot = &DAT_80612138;

    aiactor_stop_bark_slot((int*)slot);

    if (-1 < sampleId)
    {
        float vol = (frameFlags & 0x200) ? FLOAT_80615e58 : FLOAT_80615e54;
        double reverb = (double)sound_set_reverb(sampleId);
        unsigned int handle = (unsigned int)sound_play_or_resume_3d(
            (double)FLOAT_80615e4c, (double)FLOAT_80615e50, (double)vol, sampleId, 0, 1);
        *slot = handle;
        if ((frameFlags & 0x10000) != 0)
            sound_set_pitch((double)FLOAT_80615e4c, (int)handle);
        if (barkRec != (int*)0)
            barkRec[8] = (int)handle;

        int timer = game_get_round_timer();
        if ((timer != 0) && (soundId != 0))
        {
            int owner = (barkRec == (int*)0) ? 0 : *(int*)(barkRec[0] + 0x1c);
            for (int player = 0; player < DAT_80612610; ++player)
            {
                hud_create_multiline_text(
                    (double)FLOAT_80615e4c, reverb, (double)vol,
                    0.0, 0.0, 0.0, 0.0, 0.0,
                    player, -2, 9, soundId, owner, (int)handle, p9, p10);
            }
            slot[1] = 0xfffffffe;
            (void)owner;
        }
    }
    if ((recBase != 0) && (*(void**)(recBase + 0x38) != (void*)0))
        (*(void(*)(int*))*(void**)(recBase + 0x38))(barkRec);

    __restore_gpr_25();
}

// ---------------------------------------------------------------------------
// aiactor_get_bark_def_for_mode (0x801430c0)
// Resolve the bark-definition table entry (&DAT_8040dbd8 + idx*0xb) for the
// current game mode (DAT_80612614) and this actor's dialogue-set 4CC tag
// (aiObject +0xbf0 -> +8). Returns 0 when the mode/tag has no bark set.
int* aiactor_get_bark_def_for_mode(int actor)
{
    int idx = -1;
    int aiObject = *(int*)(actor + 0x7c);

    switch (DAT_80612614)
    {
    default:
        return (int*)0;
    case 1:
        if (FUN_8027eb3c(aiObject) != 0) idx = 0xe;
        break;
    case 2:
        idx = 0;
        break;
    case 4:
        if (FUN_8027eb3c(aiObject) != 0) idx = 1;
        break;
    case 5:
        if (FUN_80364290(aiObject, 1) != 0) idx = 7;
        {
            int tag = *(int*)(*(int*)(aiObject + 0xbf0) + 8);
            if ((tag == 0x52534247) || (tag == 0x52533032)) // 'RSBG' / 'RS02'
                idx = 6;
        }
        break;
    case 6:
        if (FUN_8027eb3c(aiObject) != 0) idx = 5;
        break;
    case 9:
        if (FUN_8027eb3c(aiObject) != 0) idx = 0xb;
        break;
    case 0xb:
        idx = (FUN_8027eb3c(aiObject) == 0) ? 4 : 3;
        break;
    case 0xe:
        if ((FUN_8027eb3c(aiObject) != 0) && (FUN_80220f30(0xb) != 0)) idx = 10;
        break;
    case 0x11:
        if (FUN_8027eb3c(aiObject) != 0) idx = 0xf;
        break;
    case 0x12:
        if (FUN_8027eb3c(aiObject) != 0) idx = 2;
        break;
    case 0x16:
        if (FUN_8027eb3c(aiObject) != 0) idx = 0x10;
        break;
    case 0x1a:
        idx = (*(int*)(*(int*)(aiObject + 0xbf0) + 8) == 0x5054435a) ? 9 : 8; // 'PCTZ'
        break;
    case 0x51:
        {
            int tag = *(int*)(*(int*)(aiObject + 0xbf0) + 8);
            if ((FUN_8027eb3c(aiObject) != 0) || (tag == 0x52313142)) // 'R11B'
                idx = 0xc;
            if (tag == 0x4352545a) // 'CRTZ'
                idx = 0xd;
        }
        break;
    }

    if (idx < 0)
        return (int*)0;
    return &DAT_8040dbd8 + idx * 0xb;
}
