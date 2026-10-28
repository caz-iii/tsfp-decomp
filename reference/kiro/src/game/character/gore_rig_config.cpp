// Address range: 0x80364098 - 0x80364807 | batch_1024 | 2026-09-12
//
// Character difficulty/team config accessors + gore-piece rig precompute.
//
// A small group of lookups keyed off the global game-state flags
// (DAT_805015bd = game mode, DAT_805015bf = difficulty/skill byte,
// DAT_805015c0 = feature bitfield; bit 0x80000 = "per-character config"
// active, bit 0x40 = force-team, bit 0x20000000 = skip-preload) plus the
// two heavy routines that build and preload the 7 dismemberment ("gore
// piece") skeleton rigs used when a character is torn apart.
//
//   difficulty_index          0x80364098  normalized skill index 0..4 (cached)
//   fmt_difficulty_string     0x8036412c  printf wrapper w/ difficulty token
//   load_difficulty_pak       0x803641b0  load the skill-specific sounds pak
//   char_config_scale         0x803641f8  difficulty/config-scaled tuning value
//   char_config_word          0x8036424c  per-character config dword (or 0)
//   char_team_flags_entity    0x80364290  team-flag test by entity
//   char_team_flags           0x803642b4  team-flag test by team index + mask
//   char_team_color           0x80364324  resolve a character's team/colour id
//   gore_rig_build            0x80364428  precompute the 7 gore-piece rigs
//   gore_rig_preload          0x80364774  preload the 7 gore-piece rig resources
//
// NOTE: non-matching decomp. The 7 rig descriptor tables live in rodata as an
// array of records (stride 0x4dc) at DAT_804920d8; several member offsets are
// accessed as raw pointers pending a proper struct.
#include "common.h"
#include "generated_decls.h"

// ---- game-state config globals ----
extern unsigned char DAT_805015bd;   // game mode (0x0a == arcade/custom)
extern unsigned char DAT_805015bf;   // difficulty / skill byte
extern unsigned int  DAT_805015c0;   // feature bitfield
extern unsigned char DAT_805015a0[]; // per-character-class default team table
extern int           DAT_8060f948;   // cached normalized difficulty index
extern int           DAT_80611824;   // animation-set handle table base
extern int           DAT_80612614;   // current level id
extern int           DAT_806112ac;   // "!"-prefixed format literal
extern int           DAT_806112b4;   // plain format literal

// ---- rodata tables ----
extern int   PTR_DAT_8048a90c;              // difficulty-string table (stride 2 dwords)
extern int   PTR_s_pak_sounds_e_pak_8048a910; // difficulty sounds-pak path table
extern float DAT_8048eba8;                  // per-config scale table (stride 0x14)
extern unsigned int DAT_8048ebb8;           // per-character team-flag table (stride 0x14)
extern unsigned int DAT_8048ca8c;           // per-character team-colour table (stride 0x30)
extern int   DAT_8048f97c;                  // per-level team override table (stride 0xa4)
extern short DAT_804d1cd0;                  // gore-rig anim-set id table
extern int   DAT_804920d8;                  // gore-rig descriptor array base (stride 0x4dc)
extern int   PTR_DAT_804920e4;              // rig: bone descriptor list ptr
extern int   PTR_DAT_804920e8;              // rig: joint constraint list ptr
extern int   PTR_DAT_804920ec;              // rig: bone->anim remap list ptr
extern int   PTR_DAT_804920f0;              // rig: parent-index list ptr

// ---- rodata float constants ----
extern float FLOAT_8061b218, FLOAT_8061b21c, FLOAT_8061b220;

// ---- raw helpers (not yet decompiled) ----
extern void FUN_80387fb8(unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, unsigned long long,
                         unsigned long long, unsigned long long, int, void*, int,
                         int, unsigned int, unsigned int, unsigned int, unsigned int);
extern void FUN_8038b510(int* dst, int* src);
extern void FUN_803826c4(void);              // scratch scope enter (gore_rig_build)
extern unsigned long long FUN_803826e4(void);// scratch scope enter (preload; ctx pair)
extern void FUN_80382710(void);              // scratch scope leave
extern void FUN_80382730(void);              // scratch scope leave

// entity_preload_resources is declared 1-arg void in generated_decls.h but is
// really a chained-handle call; alias it so we can pass the ctx + type id and
// thread the returned handle.
typedef unsigned long long (*PreloadFn)(unsigned long long, unsigned long long, unsigned long long,
                                        unsigned long long, unsigned long long, unsigned long long,
                                        unsigned long long, unsigned long long, int);
// mtx4_to_quat's real form takes (matrix, outQuat); the decompiler dropped the
// args. Alias to the true 2-arg form.
typedef void (*Mtx4ToQuatFn)(float* mtx, float* outQuat);

// -------------------------------------------------------------------------
// 0x80364098  difficulty_index - normalized skill index 0..4 (cached).
//   Clamps the raw skill value from boot_ret_zero_a() into the 0..4 range,
//   caches it in DAT_8060f948, and returns |index|.
// -------------------------------------------------------------------------
unsigned int difficulty_index(void)
{
    int raw = boot_ret_zero_a();
    if (raw == 2) {
        DAT_8060f948 = 3;
    } else if (raw < 2) {
        if (raw == 0)       DAT_8060f948 = 0;
        else if (raw > -1)  DAT_8060f948 = 1;
    } else if (raw == 4) {
        DAT_8060f948 = 4;
    } else if (raw < 4) {
        DAT_8060f948 = 2;
    }
    return (unsigned int)DAT_8060f948 & (0xffffffffU - ((unsigned int)((int)DAT_8060f948 >> 0x1f)));
}

// -------------------------------------------------------------------------
// 0x8036412c  fmt_difficulty_string - printf wrapper with difficulty token.
//   If the format begins with '!', substitute the difficulty-specific string
//   from the PTR_DAT_8048a90c table (indexed by difficulty_index) and skip the
//   marker; otherwise pass the format through unchanged.
// -------------------------------------------------------------------------
void fmt_difficulty_string(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                           unsigned long long a4, unsigned long long a5, unsigned long long a6,
                           unsigned long long a7, unsigned long long a8,
                           char* fmt, int a10, unsigned int a11, unsigned int a12,
                           unsigned int a13, unsigned int a14, unsigned int a15, unsigned int a16)
{
    (void)a11;
    if (*fmt == '!') {
        unsigned int idx = difficulty_index();
        FUN_80387fb8(a1, a2, a3, a4, a5, a6, a7, a8, a10, &DAT_806112ac,
                     (&PTR_DAT_8048a90c)[idx * 2], (int)(intptr_t)(fmt + 1), a13, a14, a15, a16);
    } else {
        FUN_80387fb8(a1, a2, a3, a4, a5, a6, a7, a8, a10, &DAT_806112b4,
                     (int)(intptr_t)fmt, a12, a13, a14, a15, a16);
    }
}

// -------------------------------------------------------------------------
// 0x803641b0  load_difficulty_pak - load the skill-specific sounds pak.
// -------------------------------------------------------------------------
void load_difficulty_pak(int* dst)
{
    unsigned int idx = difficulty_index();
    FUN_8038b510(dst, (int*)(&PTR_s_pak_sounds_e_pak_8048a910)[idx * 2]);
}

// -------------------------------------------------------------------------
// 0x803641f8  char_config_scale - difficulty/config-scaled tuning value.
//   Base scale depends on the difficulty byte (DAT_805015bf); when the
//   per-character config bit (0x80000) is set the base is multiplied by the
//   per-config table entry, otherwise by the flat FLOAT_8061b220 factor.
// -------------------------------------------------------------------------
double char_config_scale(int cfgIndex)
{
    float base = FLOAT_8061b218;
    if (DAT_805015bf == '\0') {
        base = FLOAT_8061b21c;
    }
    if ((DAT_805015c0 & 0x80000) != 0) {
        return (double)(base * (&DAT_8048eba8)[cfgIndex * 0x14]);
    }
    return (double)(FLOAT_8061b220 * base);
}

// -------------------------------------------------------------------------
// 0x8036424c  char_config_word - per-character config dword (or 0).
//   Returns the config word for the entity's character class (+0x48), but only
//   in arcade mode (DAT_805015bd == 0x0a) or when the per-character bit is set;
//   0 otherwise.
// -------------------------------------------------------------------------
unsigned int char_config_word(int entity)
{
    if ((DAT_805015bd != '\n') && ((DAT_805015c0 & 0x80000) == 0)) {
        return 0;
    }
    return (&DAT_8048ebb8)[*(short*)(entity + 0x48) * 0x14];
}

// -------------------------------------------------------------------------
// 0x80364290  char_team_flags_entity - team-flag test by entity.
// -------------------------------------------------------------------------
unsigned int char_team_flags_entity(int entity, unsigned int mask)
{
    return char_team_flags((int)*(short*)(entity + 0x48), mask);
}

// -------------------------------------------------------------------------
// 0x803642b4  char_team_flags - team-flag test by team index + mask.
//   Returns 1 when the "always" bit (mask bit0) is set and the class flags
//   overlap the mask; otherwise, only in arcade / per-character mode, returns
//   the masked class flags; 0 elsewhere.
// -------------------------------------------------------------------------
unsigned int char_team_flags(int teamIndex, unsigned int mask)
{
    unsigned int classFlags = (&DAT_8048ebb8)[teamIndex * 0x14];
    if (((mask & 1) != 0) && ((mask & classFlags) != 0)) {
        return 1;
    }
    if ((DAT_805015bd != '\n') && ((DAT_805015c0 & 0x80000) == 0)) {
        return 0;
    }
    return mask & classFlags;
}

// -------------------------------------------------------------------------
// 0x80364324  char_team_color - resolve a character's team/colour id.
//   With force-team (bit 0x40) returns team 8. In arcade mode it first checks
//   the per-level team override table (matched by class id at +0x48). Falls
//   back to the per-character colour table (bit 0x80000 set) or the class
//   default-team table (DAT_805015a0), the latter mapped 0/1/2 -> 0xf/0xc/9.
// -------------------------------------------------------------------------
unsigned int char_team_color(int entity)
{
    if ((DAT_805015c0 & 0x40) != 0) {
        return 8;
    }

    if (DAT_805015bd == '\n') {
        int info = (int)(intptr_t)level_get_info_cached(DAT_80612614);
        int* row = (int*)(&DAT_8048f97c + *(int*)(info + 8) * 0xa4);
        int count = *row;
        int off = 0;
        while (count > 0) {
            if (*(short*)((int)row + off + 4) == *(short*)(entity + 0x48)) {
                return (unsigned int)*(unsigned char*)((int)row + (unsigned int)DAT_805015bf + off + 6);
            }
            off += 8;
            count--;
        }
    }

    if ((DAT_805015c0 & 0x80000) == 0) {
        return (unsigned int)DAT_805015a0[*(int*)(entity + 8)] * -3 + 0xf;
    }
    return (&DAT_8048ca8c)[*(short*)(entity + 0x48) * 0x30];
}

// -------------------------------------------------------------------------
// 0x80364428  gore_rig_build - precompute the 7 dismemberment rigs.
//   For each of the 7 gore-piece rig descriptors (stride 0x4dc at DAT_804920d8)
//   this builds the bind-pose bone matrices from the rig's anim keyframes,
//   composes them with parent transforms, computes each bone's inverse-bind
//   matrix into a scratch page, then bakes each joint's relative rotation
//   (quaternion) and pivot vectors used at runtime to tear the mesh apart.
//   Runs inside a scratch scope; frees the scratch page at the end.
// -------------------------------------------------------------------------
void gore_rig_build(void)
{
    float qA[4];   // afStack_a8
    float qB[4];   // afStack_98
    float qConj[4];// afStack_88
    float trs[8];  // local_78..local_44 (translation + quat scratch)

    FUN_803826c4();
    int scratch = mem_page_alloc();
    int boneMtx = scratch + 0x200;    // per-bone world matrices
    int prevSet = -1;
    int rig = 0;

    for (int r = 0; r < 7; r++, rig += 0x4dc) {
        int* desc     = (int*)((int)&DAT_804920d8 + rig);
        int  animSet  = *(int*)((int)&DAT_804920d8 + rig + 4);
        int  anim     = *(int*)(DAT_80611824 + (short)(&DAT_804d1cd0)[animSet] * 4);

        // build bind-pose bone matrices (skip if this anim-set already built)
        if (animSet != prevSet) {
            int mOff = 0;
            int pOff = 0;
            int boneCount = *(int*)(anim + 0x14);
            for (int b = 0; b < boneCount; b++, mOff += 0x40, pOff += 4) {
                int parent = *(int*)(*(int*)((int)&PTR_DAT_804920f0 + rig) + pOff);
                anim_sample_keyframe(anim, b, 0, &trs[0]);
                float* m = (float*)(boneMtx + mOff);
                // rotation quat sits at trs[3..6] (local_6c..local_44)
                float rot[4];
                rot[0] = trs[3]; rot[1] = trs[4]; rot[2] = trs[5]; rot[3] = trs[6];
                mtx4_set_identity((int)m);
                quat_to_matrix(m, rot);
                m[0xc] = trs[0]; m[0xd] = trs[1]; m[0xe] = trs[2];
                if (parent != -1) {
                    mtx4_multiply(m, (float*)(boneMtx + parent * 0x40), m);
                }
            }
        }
        prevSet = animSet;

        // apply per-bone local pivot offset and store the inverse-bind matrix
        int invOff = 0;
        int bOff = 0;
        for (int b = 0; b < *desc; b++, invOff += 0x40, bOff += 0x1c) {
            int* bone = (int*)(*(int*)((int)&PTR_DAT_804920e4 + rig) + bOff);
            float pivot[3];
            pivot[0] = (float)bone[3];
            pivot[1] = (float)bone[4];
            pivot[2] = (float)bone[5];
            float* m = (float*)(boneMtx + *bone * 0x40);
            mtx4_transform_vector(m, pivot);
            m[0xc] += pivot[0];
            m[0xd] += pivot[1];
            m[0xe] += pivot[2];
            mtx4_invert((float*)(scratch + invOff), m);
        }

        // compose skinning matrices for each anim bone with the inverse-bind
        int cOff = 0;
        int rOff = 0;
        int boneCount = *(int*)(anim + 0x14);
        for (int b = 0; b < boneCount; b++, cOff += 0x40, rOff += 4) {
            int rebind = *(int*)(*(int*)((int)&PTR_DAT_804920ec + rig) + rOff);
            if (rebind != -1) {
                mtx4_multiply((float*)(boneMtx + cOff), (float*)(scratch + rebind * 0x40),
                              (float*)((int)desc + cOff + 0x1c));
            }
        }

        // bake each joint constraint's relative rotation + pivot vectors
        int jOff = 0;
        for (int j = 0; j < *desc - 1; j++, jOff += 0x4c) {
            int joint = *(int*)((int)&PTR_DAT_804920e8 + rig) + jOff;
            int childBone = *(int*)(*(int*)((int)&PTR_DAT_804920e4 + rig) + *(int*)(joint + 0x18) * 0x1c);
            float* m = (float*)(boneMtx + *(int*)(*(int*)((int)&PTR_DAT_804920e4 + rig) +
                                                  *(int*)(joint + 0x14) * 0x1c) * 0x40);
            ((Mtx4ToQuatFn)&mtx4_to_quat)(m, qA);
            ((Mtx4ToQuatFn)&mtx4_to_quat)((float*)(boneMtx + childBone * 0x40), qB);
            quat_conjugate(qA, qConj);
            quat_multiply((float*)(joint + 0x24), qA, qB);

            int axis = *(int*)(joint + 0x10) * 4;
            *(float*)(joint + 0x34) = m[axis];
            *(float*)(joint + 0x38) = m[axis + 1];
            *(float*)(joint + 0x3c) = m[axis + 2];
            mtx4_transform_vector_transpose(m, (float*)(joint + 0x34));
            *(float*)(joint + 0x40) = m[axis];
            *(float*)(joint + 0x44) = m[axis + 1];
            *(float*)(joint + 0x48) = m[axis + 2];
            mtx4_transform_vector_transpose((float*)(boneMtx + childBone * 0x40), (float*)(joint + 0x40));
        }
    }

    mem_page_free(boneMtx);
    FUN_80382710();
}

// -------------------------------------------------------------------------
// 0x80364774  gore_rig_preload - preload the 7 gore-piece rig resources.
//   Unless the skip-preload bit (0x20000000) is set, walks each rig's bone
//   list and preloads the referenced entity resources, chaining the loader
//   handle through the scratch context.
// -------------------------------------------------------------------------
void gore_rig_preload(unsigned long long a1, unsigned long long a2, unsigned long long a3,
                      unsigned long long a4, unsigned long long a5, unsigned long long a6,
                      unsigned long long a7, unsigned long long a8)
{
    (void)a1;
    unsigned long long h = FUN_803826e4();
    if ((DAT_805015c0 & 0x20000000) == 0) {
        int rig = 0;
        for (int r = 0; r < 7; r++, rig += 0x4dc) {
            int bOff = 0;
            int boneCount = *(int*)((int)&DAT_804920d8 + rig);
            for (int b = 0; b < boneCount; b++, bOff += 0x1c) {
                int typeId = *(int*)(*(int*)((int)&PTR_DAT_804920e4 + rig) + bOff + 4);
                h = ((PreloadFn)&entity_preload_resources)(h, a2, a3, a4, a5, a6, a7, a8, typeId);
            }
        }
    }
    FUN_80382730();
}
