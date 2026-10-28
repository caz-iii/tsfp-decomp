// Address: 0x800a3dec-0x800a3fbc | Name: player_spawn | Date: 2026-08-11
// Player spawn system. Weapon selection and zone selection on respawn.
// Handles custom loadouts, team presets, match weapon lists, mapmaker tool,
// and hub-based/random spawn zone selection.

#include "common.h"

extern unsigned int DAT_805015c0; // Game flags
extern char DAT_805015bd;         // Region/mode byte
extern char DAT_805015be;         // Sub-mode byte
extern int DAT_80612614;          // Level ID
extern int DAT_80611694;          // Spawn override flag

extern int FUN_803826e8(void);
extern void FUN_80382734(void);
extern int FUN_8022a8a0(void);  // get_custom_loadout_data
extern int match_build_weapon_list(void);
extern void FUN_801a79dc(void); // weapon_clear_slots
extern void FUN_801a9384(void); // weapon_finalize
extern void FUN_801a95b8(int pCharData, int weaponId);
extern void FUN_801a99e0(int pCharData, int weaponId, short* pAmmo);
extern int soundpad_find_zone_by_id(int zoneId);
extern int level_get_info_cached(int levelId);
extern int spawn_check_flags_hub(unsigned int flags);
extern void spawn_select_character_wrapper(void);
extern int FUN_8025b8c0(int param);
extern void FUN_8025b5dc(int playerIdx);
extern void FUN_8013104c(void); // random spawn

// ============================================================================
// Address: 0x800a3dec | player_select_spawn_weapon
// Selects initial weapon on spawn. Priority: custom loadout (flag 0x40),
// team preset (0x8000000), match weapon list (0x400), or default (weapon 1).
// Mapmaker (level 100, GCN, P0) gets tool weapon 0x38.
// ============================================================================
void player_select_spawn_weapon(void) {
    int* pPlayerData = (int*)FUN_803826e8();
    int weaponId = 0;
    int pCharData = *(int*)(pPlayerData[5] + 0x7C);

    // Custom loadout or team preset
    if ((DAT_805015c0 & 0x40) != 0 || (DAT_805015c0 & 0x8000000) != 0) {
        int pLoadout = FUN_8022a8a0();
        weaponId = *(int*)(pLoadout + pPlayerData[1] * 0xC + 0x74);
    }

    // Match weapon list fallback
    if (weaponId == 0 && (DAT_805015c0 & 0x400) != 0) {
        int pWeaponList = match_build_weapon_list();
        int listCount = *(int*)(pWeaponList + 4);
        if (listCount > 0) {
            int firstWeapon = *(int*)(pWeaponList + 8);
            // Check if weapon has flag 0x10 (dual-wield marker)
            if (firstWeapon > 0 && (*(int*)(0x8043E22C + firstWeapon * 0x3C) & 0x10) == 0) {
                weaponId = firstWeapon;
            }
            // If dual-wield flagged: clear and finalize
            if (listCount > 0 && firstWeapon > 0 &&
                (*(int*)(0x8043E22C + firstWeapon * 0x3C) & 0x10) != 0) {
                FUN_801a79dc(); // weapon_clear_slots
                FUN_801a9384(); // weapon_finalize
            }
        }
    }

    // Default weapon (if no flags match and not no-weapon mode)
    if (weaponId == 0 && (DAT_805015c0 & 0x2000) == 0) {
        weaponId = 1; // Default pistol
    }

    // Mapmaker tool override (level 100, NTSC, player 0)
    if (DAT_80612614 == 100 && DAT_805015bd == '\n' && pPlayerData[0] == 0) {
        weaponId = 0x38; // Mapmaker tool
    }

    // Equip weapon
    if (weaponId > 0 && weaponId < 0x46) {
        if (((DAT_805015c0 & 0x40) == 0 && (DAT_805015c0 & 0x8000000) == 0) ||
            /* loadout slot empty */(*(int*)(FUN_8022a8a0() + pPlayerData[1] * 0xC + 0x74) == 0)) {
            FUN_801a79dc(); // weapon_clear_slots
        } else {
            // Custom loadout: equip with ammo from loadout data
            int pLoadout = FUN_8022a8a0();
            FUN_801a99e0(pCharData, weaponId, (short*)(pLoadout + pPlayerData[1] * 0xC + 0x78));
        }
        FUN_801a9384(); // weapon_finalize
        FUN_801a95b8(pCharData, weaponId); // weapon_activate
    }

    FUN_80382734(); // Restore PS regs
}

// ============================================================================
// Address: 0x800a3fbc | player_select_spawn_zone
// Selects spawn zone. Priority: custom config zone, level spawn table
// (flags filtered), hub-based, or random.
// ============================================================================
void player_select_spawn_zone(void) {
    int* pPlayerData = (int*)FUN_803826e8();
    int zoneIndex = -1;

    // Custom loadout or team preset: explicit zone
    if ((DAT_805015c0 & 0x40) != 0 || (DAT_805015c0 & 0x8000000) != 0) {
        int pLoadout = FUN_8022a8a0();
        int zoneId = *(int*)(pLoadout + pPlayerData[1] * 0xC + 0x70);
        if (zoneId >= 0) {
            zoneIndex = soundpad_find_zone_by_id(zoneId);
        }
    }

    if (zoneIndex == -1) {
        if (DAT_80611694 != 0) {
            // Override spawn (scripted)
            FUN_8013104c(); // random/scripted spawn
        } else if (DAT_805015be == '\v' &&
                   (*(unsigned int*)(DAT_80611d74 + 0xC) & 0x1000) != 0) {
            // Clear respawn-at-checkpoint flag
            *(unsigned int*)(DAT_80611d74 + 0xC) &= 0xFFFFEFFF;
        } else if (DAT_805015bd == '\n') {
            // NTSC: level spawn table lookup
            int pLevelInfo = level_get_info_cached(DAT_80612614);
            int playerIdx = pPlayerData[0];

            int hubCheck = FUN_8025b8c0(0);
            if (hubCheck == 0) {
                // Iterate spawn entries, check flags, pick Nth valid
                int spawnCount = *(int*)(pLevelInfo + 0x28);
                int* pSpawnTable = (int*)*(int*)(pLevelInfo + 0x24);
                int entryOffset = 0;
                for (int i = 0; i < spawnCount; i++) {
                    int valid = spawn_check_flags_hub(*(unsigned int*)((int)pSpawnTable + entryOffset));
                    if (valid != 0) {
                        if (playerIdx == 0) {
                            int spawnZoneId = *(int*)((int)pSpawnTable + entryOffset + 4);
                            soundpad_find_zone_by_id(spawnZoneId);
                            break;
                        }
                        playerIdx--;
                    }
                    entryOffset += 0x20;
                }
            } else {
                // Hub-based spawn
                FUN_8025b5dc(pPlayerData[0]);
            }
        } else {
            // PAL/other: character wrapper spawn
            spawn_select_character_wrapper();
        }
    }

    FUN_80382734(); // Restore PS regs
}
