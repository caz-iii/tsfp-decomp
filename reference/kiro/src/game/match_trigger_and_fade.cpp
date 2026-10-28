// match_trigger_and_fade.cpp
// Match-mode trigger/squad reference scanning plus the HUD screen-fade state
// machine, a small contiguous block in the match-mode tick group.
//
//   - match_slot_flag_a / match_slot_flag_b: read the "not-disabled" bit (bit 3,
//     inverted) of a slot record in the two 0x10-stride arrays hung off the mode
//     context DAT_80612db0 (+0xa0 and +0x98).
//   - match_check_trigger_squads (+ its thin wrapper) and match_check_squad_refs:
//     walk the current level's trigger-block table (level_get_info_cached +0x12c
//     count / +0x130 base, 0xb4-stride) and squad-ref block table (+0x134 count /
//     +0x138 base, 0x220-stride) looking for a block that references the current
//     editor source (FUN_803826e8) via the match_block_refs_squad_* helpers.
//   - match_hud_fade_begin / match_hud_fade_advance / match_hud_fade_draw (+ two
//     empty enter/leave hooks): the screen-fade used at match start/end. begin
//     seeds the fade level (FLOAT_80612db8) to a start constant; advance steps it
//     by a per-frame delta scaled by the frame counter DAT_806109c0 (via the
//     classic int->double CONCAT44 idiom); draw renders a full-viewport fade quad
//     through the GX helpers.
//
// Address range: 0x803546dc - 0x80354908  (batch_1010)
//
// Self-contained translation unit: raw FUN_/DAT_/FLOAT_/DOUBLE_ symbols are
// declared file-locally with concrete types; named cross-module helpers resolve
// at link time.

typedef unsigned int   undefined4;
typedef unsigned int   uint;

static inline unsigned long long CONCAT44(unsigned int hi, unsigned int lo) {
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
}

extern "C" {

// ---- named cross-module helpers (concrete signatures) ----
int  level_get_info_cached(int levelId);
int  match_block_refs_squad_88(int src, int blockBase, int refRec);
int  match_block_refs_squad_04(int src, int blockBase, int refRec);
void gx_set_scissor_rect(int x, int w, int y, int h);
void gx_set_sub_material_id(int id);
void gx_set_blend_mode(int mode);
void gx_fill_rect_clipped(int x, int y, int w, int h, int color);
void sprite_draw_viewport_quad(void);

// ---- raw same-cluster callees ----
int  FUN_803826e8(void);   // enter editor-source ctx -> current source id
void FUN_80382734(void);   // leave editor-source ctx
int  FUN_803547c8(void);   // squad-ref scan (== match_check_squad_refs)
void FUN_8035471c(void);   // trigger-squad scan (== match_check_trigger_squads)
void FUN_80354980(void);   // fade-begin sub-init
void FUN_80354994(void);   // fade-advance sub-step
void FUN_80354af4(void);   // fade-quad material/texture setup

// ---- raw globals ----
extern int   DAT_80612db0;   // mode/stat context base (slot arrays at +0x98/+0xa0)
extern int   DAT_80612614;   // current level id
extern int   DAT_806109c0;   // fade frame counter (int)
extern int   DAT_806109c4;   // screen width
extern int   DAT_806109c8;   // screen height
extern int  *DAT_80611748;   // active viewport rect descriptor
extern float FLOAT_806108ec;  // fade speed base
extern float FLOAT_80612db8;  // current fade level (state)
extern float FLOAT_8061ad80;  // fade start level
extern float FLOAT_8061ad84;  // fade delta numerator
extern double DOUBLE_8061ad88; // int->double conversion bias (0x4330000080000000)

// ============================================================================
// 0x803546dc  match_slot_flag_a
// Return the inverted bit-3 of the flag word (+8) of slot param_1 in the
// 0x10-stride array at mode context +0xa0 (1 = enabled / not-disabled).
// ============================================================================
uint match_slot_flag_a(int param_1)
{
    return (*(uint *)(*(int *)(DAT_80612db0 + 0xa0) + param_1 * 0x10 + 8) >> 3 & 1) ^ 1;
}

// ============================================================================
// 0x803546fc  match_slot_flag_b
// Same as match_slot_flag_a but for the slot array at mode context +0x98.
// ============================================================================
uint match_slot_flag_b(int param_1)
{
    return (*(uint *)(*(int *)(DAT_80612db0 + 0x98) + param_1 * 0x10 + 8) >> 3 & 1) ^ 1;
}

// ============================================================================
// 0x803547c8  match_check_squad_refs
// Walk the current level's squad-ref block table (level +0x134 count, +0x138
// base, 0x220-stride) for a block that references the current editor source.
// Block type 2 uses the +0x88 ref list, type 1 the +0x4c-offset +0x04 ref list.
// Returns nonzero on the first matching block. (Decompiler lost the return; the
// match result iVar5 is the meaningful value the caller tests.)
// ============================================================================
int match_check_squad_refs(void)
{
    int  blockType;
    int *src;
    int  info;
    int  matched;
    int  off;
    int  refRec;

    src     = (int *)FUN_803826e8();
    matched = 0;
    info    = level_get_info_cached(DAT_80612614);
    off     = 0;
    for (int i = 0; i < *(int *)(info + 0x134); i = i + 1) {
        blockType = *src;
        refRec    = *(int *)(info + 0x138) + off;
        if (blockType == 2) {
            matched = match_block_refs_squad_88((int)src, *(int *)(info + 0x130), refRec);
        } else if ((blockType < 2) && (0 < blockType)) {
            matched = match_block_refs_squad_04((int)src, *(int *)(info + 0x130), refRec + 0x4c);
        }
        if (matched != 0) break;
        off = off + 0x220;
    }
    FUN_80382734();
    return matched;
}

// ============================================================================
// 0x8035471c  match_check_trigger_squads
// Walk the current level's trigger-block table (level +0x12c count, +0x130 base,
// 0xb4-stride) for a block whose id (+0xc short) matches the current editor
// source and whose squad refs also match (match_check_squad_refs).
// ============================================================================
void match_check_trigger_squads(void)
{
    int src;
    int info;
    int i;
    int off;
    int ok;

    src  = FUN_803826e8();
    info = level_get_info_cached(DAT_80612614);
    i    = 0;
    off  = 0;
    while ((i < *(int *)(info + 300) &&
            ((*(short *)(*(int *)(info + 0x130) + off + 0xc) != src ||
              (ok = FUN_803547c8(), ok == 0))))) {
        i   = i + 1;
        off = off + 0xb4;
    }
    FUN_80382734();
}

// ============================================================================
// 0x803547a8  match_check_trigger_squads_wrap
// Thin forwarder to match_check_trigger_squads.
// ============================================================================
void match_check_trigger_squads_wrap(void)
{
    FUN_8035471c();
}

// ============================================================================
// 0x80354880  match_hud_fade_enter  (empty hook)
// ============================================================================
void match_hud_fade_enter(void)
{
}

// ============================================================================
// 0x80354884  match_hud_fade_begin
// Run the fade sub-init and seed the current fade level to the start constant.
// ============================================================================
void match_hud_fade_begin(void)
{
    FUN_80354980();
    FLOAT_80612db8 = FLOAT_8061ad80;
}

// ============================================================================
// 0x803548ac  match_hud_fade_leave  (empty hook)
// ============================================================================
void match_hud_fade_leave(void)
{
}

// ============================================================================
// 0x803548b0  match_hud_fade_advance
// Advance the fade level by (speed_base * (delta / frameCounter)) using the
// classic PPC int->double conversion idiom for the frame counter, then run the
// fade sub-step.
// ============================================================================
void match_hud_fade_advance(void)
{
    FLOAT_80612db8 =
        FLOAT_806108ec *
        (FLOAT_8061ad84 /
         (float)((double)(long long)CONCAT44(0x43300000, DAT_806109c0 ^ 0x80000000) - DOUBLE_8061ad88)) +
        FLOAT_80612db8;
    FUN_80354994();
}

// ============================================================================
// 0x80354908  match_hud_fade_draw
// Render the full-viewport fade quad: set the scissor to the active viewport
// rect, configure material/blend, run the fade-quad setup, draw the viewport
// quad, then fill the whole screen rect clipped with mask 0xf.
// ============================================================================
void match_hud_fade_draw(void)
{
    gx_set_scissor_rect(DAT_80611748[0], DAT_80611748[5], DAT_80611748[1], DAT_80611748[6]);
    gx_set_sub_material_id(1);
    gx_set_blend_mode(0);
    FUN_80354af4();
    sprite_draw_viewport_quad();
    gx_set_sub_material_id(1);
    gx_fill_rect_clipped(0, 0, DAT_806109c4, DAT_806109c8, 0xf);
}

} // extern "C"
