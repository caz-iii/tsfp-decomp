// level_def_table.cpp
// Accessors over the game level-definition table at DAT_8047fd70.
// Each record is 0x58 bytes, indexed by level / sublevel id. Fields observed:
//   +0x00 word  : level name string id       (level_def_name_string_id)
//   +0x04 word  : base preload resource id    (level_def_preload_base_id)
//   +0x08 u16   : music track id              (level_def_music_track_id)
//   +0x0a byte  : default entity/weapon id    (level_def_default_entity)
//   +0x0b byte  : flag byte b (level def +0x14 source) (level_def_flag_b)
//   +0x0c byte  : flag byte c (level def +0x1c source) (level_def_flag_c)
//   +0x50 word  : loading-screen background texture id (read directly at rec+0x50)
//   +0x54 word  : aux word                    (level_def_aux_word)
// Consumers: level_preload_all_resources_pass, loadscreen_select_background_texture,
//            music_get_level_track_id, arcade map-settings menu builder (FUN_8031e898).
//
// Address: 0x8031d42c..0x8031d5d4 | Batch: batch_896 | Date: 2026-09-12

#include "common.h"

// ---- raw symbols referenced by this translation unit ----
extern unsigned char DAT_8047fd70;          // level-definition table base (stride 0x58)
extern int get_string_by_id(int stringId);

// Empty guard/assert stub (compiled out of the retail build): 0x8031d464 -> blr.
void level_def_table_guard(void)
{
    return;
}

// 0x8031d42c: return 1 if the group/arc id is one of the five story arcs (1..5).
int level_def_is_valid_arc(int arcId)
{
    if (((arcId != 1) && (arcId != 2) && (arcId != 3)) && ((arcId != 4) && (arcId != 5)))
    {
        return 0;
    }
    return 1;
}

// 0x8031d468: pointer to the level-definition record for a level id.
unsigned char *level_def_record(int levelId)
{
    return &DAT_8047fd70 + levelId * 0x58;
}

// 0x8031d47c: aux word at record +0x54.
unsigned int level_def_aux_word(int levelId)
{
    return *(unsigned int *)(&DAT_8047fd70 + 0x54 + levelId * 0x58);
}

// 0x8031d494: resolve the level name string.
int level_def_name_string(int levelId)
{
    level_def_table_guard();
    return get_string_by_id(*(int *)(&DAT_8047fd70 + levelId * 0x58));
}

// 0x8031d4d4: base preload resource id at record +0x04.
unsigned int level_def_preload_base_id(int levelId)
{
    level_def_table_guard();
    return *(unsigned int *)(&DAT_8047fd70 + 0x04 + levelId * 0x58);
}

// 0x8031d514: music track id (u16) at record +0x08.
unsigned short level_def_music_track_id(int levelId)
{
    level_def_table_guard();
    return *(unsigned short *)(&DAT_8047fd70 + 0x08 + levelId * 0x58);
}

// 0x8031d554: default entity/weapon id (byte) at record +0x0a.
unsigned char level_def_default_entity(int levelId)
{
    level_def_table_guard();
    return (&DAT_8047fd70)[0x0a + levelId * 0x58];
}

// 0x8031d594: flag byte b at record +0x0b.
unsigned char level_def_flag_b(int levelId)
{
    level_def_table_guard();
    return (&DAT_8047fd70)[0x0b + levelId * 0x58];
}

// 0x8031d5d4: flag byte c at record +0x0c.
unsigned char level_def_flag_c(int levelId)
{
    level_def_table_guard();
    return (&DAT_8047fd70)[0x0c + levelId * 0x58];
}

// ---- additional level-def accessors (batch_897) ----
// Field map continued:
//   +0x0d byte  : flag byte d (weapon-set/config selector) (level_def_flag_d)
//   +0x0e byte  : flag byte e                              (level_def_flag_e)
//   +0x10 .. +0x4b (0x3c bytes) : copied block             (level_def_copy_block10)

extern void leaf_31e8(unsigned int dst, unsigned int src, int len);

// 0x8031d614: flag byte at record +0x0d (weapon-set/config selector; read into the
// editor current-weapon-set global via aveditor_set_current_weapon_set).
unsigned char level_def_flag_d(int levelId)
{
    level_def_table_guard();
    return (&DAT_8047fd70)[0x0d + levelId * 0x58];
}

// 0x8031d654: flag byte at record +0x0e.
unsigned char level_def_flag_e(int levelId)
{
    level_def_table_guard();
    return (&DAT_8047fd70)[0x0e + levelId * 0x58];
}

// 0x8031d694: copy the 0x3c-byte block at record +0x10 into dst.
void level_def_copy_block10(unsigned int dst, int levelId)
{
    leaf_31e8(dst, levelId * 0x58 + 0x8047fd80, 0x3c);
    return;
}
