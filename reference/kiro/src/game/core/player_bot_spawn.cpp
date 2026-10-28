// player_bot_spawn.cpp - Co-op / AI-companion character spawn setup at level start.
//
// bot_spawn_coop_characters is run from aiactor_spawn_all (0x801358b4). It does
// two passes:
//   1. For every level character with a default weapon and no alt-model, spawn a
//      throwaway bot to force-load its resources, strip its weapons, then destroy
//      it (resource-priming pass).
//   2. For every scripted spawn record in the level owner table, spawn the real
//      AI companion, bind it to its spawn descriptor (+0xbf0), register it into
//      its entity group, and assign its team.
//
// Address: 0x8025c6c8

#include "common.h"

extern "C" {
extern int DAT_8061208c;    // spawn owner context (+0x2c records 0x98 stride, +0x30 count)
extern int DAT_806120fc;    // cached scripted-spawn count
extern int DAT_80612610;    // active player count (bot player-index base)
extern int DAT_80588a1c;    // spawned-companion pointer table (stride 4)

extern const float FLOAT_80618f98;   // 0.0

extern int  level_get_char_count(void);
extern void level_get_default_weapon(unsigned int charId, int* outWeapon, int* outAmmo);
extern int  player_ai_has_alt_model(int charId);
extern int* player_ai_spawn_bot(double x, double y, double z, double w,
        unsigned long long f5, unsigned long long f6, unsigned long long f7, unsigned long long f8,
        int weapon, int ammo, int playerIndex, int spawnId, int isScripted, int slot);
extern void player_ai_drop_all_weapons(unsigned long long f1, double a, double b, double c,
        unsigned long long f5, unsigned long long f6, unsigned long long f7, unsigned long long f8,
        int* actor);
extern void player_ai_assign_team(int object);
extern void entity_destroy_with_event(void* entity);
extern void entity_group_register(int groupId, int entity);
extern void spawn_stub_noop(void);
extern void FUN_803826e8(void);
extern void FUN_80382734(void);
}

// ---------------------------------------------------------------------------
// 0x8025c6c8
void bot_spawn_coop_characters(void)
{
    int* bot;
    int charCount;
    int* actor;
    int i;
    int outWeapon;
    int outAmmo;
    int weaponBuf[7];
    int ammoBuf;
    int recOff;
    int tblOff;
    int spawnResult;
    int owner;
    unsigned long long f5, f6, f7, f8;

    f5 = 0; f6 = 0; f7 = 0; f8 = 0;
    FUN_803826e8();
    DAT_806120fc = *(int*)(DAT_8061208c + 0x30);

    // Pass 1: resource-priming spawn/destroy for each level character.
    for (i = 0; (charCount = level_get_char_count(), i < charCount); i = i + 1) {
        level_get_default_weapon((unsigned int)i, weaponBuf, &outWeapon);
        if ((weaponBuf[0] != -1) && (player_ai_has_alt_model(weaponBuf[0]) == 0)) {
            double zero = (double)FLOAT_80618f98;
            bot = player_ai_spawn_bot(zero, zero, zero, zero, f5, f6, f7, f8,
                weaponBuf[0], outWeapon, i + DAT_80612610, -1, 0, -1);
            actor = (int*)bot[0x1f];
            actor[7] = 0;
            actor[6] = 0;
            actor[5] = 0;
            actor[4] = 0;
            bot[10] = bot[10] & 0xffffff9e;
            player_ai_drop_all_weapons((unsigned long long)0, zero, zero, zero, f5, f6, f7, f8,
                actor);
            if ((void*)actor[0x2f1] != (void*)0) {
                entity_destroy_with_event((void*)actor[0x2f1]);
                actor[0x2f1] = 0;
            }
            entity_destroy_with_event(bot);
            *actor = 0;
        }
    }

    // Pass 2: spawn the real scripted AI companions and register them.
    recOff = 0;
    tblOff = 0;
    for (i = 0; i < *(int*)(DAT_8061208c + 0x30); i = i + 1) {
        owner = *(int*)(DAT_8061208c + 0x2c) + recOff;
        level_get_default_weapon((unsigned int)*(unsigned short*)(owner + 0x16),
            &outWeapon, &ammoBuf);
        double zero = (double)FLOAT_80618f98;
        spawnResult = (int)player_ai_spawn_bot(zero, zero, zero, zero, f5, f6, f7, f8,
            outWeapon, ammoBuf, i + DAT_80612610, *(int*)(owner + 0x10), 1, -1);
        *(int*)((int)&DAT_80588a1c + tblOff) = spawnResult;
        *(int*)(*(int*)(*(int*)((int)&DAT_80588a1c + tblOff) + 0x7c) + 0xbf0) = owner;
        *(int*)(*(int*)(*(int*)((int)&DAT_80588a1c + tblOff) + 0x7c) + 0xbf4) = 0;
        entity_group_register(*(int*)(owner + 8), *(int*)((int)&DAT_80588a1c + tblOff));
        player_ai_assign_team(*(int*)(*(int*)((int)&DAT_80588a1c + tblOff) + 0x7c));
        tblOff = tblOff + 4;
        recOff = recOff + 0x98;
    }
    spawn_stub_noop();
    FUN_80382734();
}
