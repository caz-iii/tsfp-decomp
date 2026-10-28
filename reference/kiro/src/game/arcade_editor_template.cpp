// arcade_editor_template.cpp
// Arcade / challenge map-editor template configuration and grid placement.
// This cluster resolves per-challenge editor settings (from the challenge record,
// the mode loadout, or the global settings block), scans the challenge record's
// sections for template references, and rotates/validates a placed template's
// cell grid inside the arcade map editor.
//
//   - challenge_lookup_setting_short / challenge_resolve_editor_setting /
//     challenge_get_editor_setting: the short-valued sibling of the numeric
//     setting resolution in challenge_stat_completion.cpp, keyed off the same
//     challenge record (section 0xc77668) and DAT_80612ae8 loadout flag.
//   - challenge_template_referenced / challenge_check_current_template: walk the
//     variable-stride section list (each section: id at +0, byte size at +4) for
//     the 0xc7766e reference sections and test whether a given template index is
//     referenced by the current selection (0xc7766d id sections).
//   - aveditor_rotate_grid_scan / aveditor_tick_rotate_template: rotate a copied
//     template cell grid (0x140-byte rows) by repeatedly calling the cell rotate
//     helper, then scan the rotated footprint against the editor grid for a
//     blocked cell (bit 0x1000).
//   - arcsel_entity_layout_desc / arcsel_copy_short_list / arcsel_validate_placement:
//     small arcade-select helpers (layout descriptor lookup, count-prefixed short
//     list copy, and placement validation via the layout callback + match mode).
//
// Address range: 0x8035403c - 0x8035465c  (batch_1009)
//
// Self-contained translation unit: raw FUN_/DAT_ symbols are declared file-locally
// with concrete types matching each call site; named cross-module helpers resolve
// at link time.

typedef unsigned int   undefined4;
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  byte;
typedef unsigned char  undefined;
typedef unsigned char  undefined1;
typedef unsigned short undefined2;
typedef unsigned long long undefined8;
typedef unsigned long long ulonglong;
typedef void           code;

static inline unsigned int countLeadingZeros(unsigned int x) {
    unsigned int n = 0;
    if (x == 0) return 32;
    while ((x & 0x80000000u) == 0) { n++; x <<= 1; }
    return n;
}

extern "C" {

// ---- named cross-module helpers (concrete signatures) ----
int   *challenge_record_find_section(int recordBase, int sectionKey);
int   *challenge_record_find_match(int *rec, int recordBase, int index);
int   *menu_slot_option_table_resolve(int optionId);
undefined *game_get_settings_ptr(void);
undefined *arcsel_get_layout_desc(int descIndex);
uint   match_mode_is_type5(int modeByte);
void   aveditor_rotate_template_dims(uint dstGrid, uint tmpl);
void   aveditor_rotate_cell(short *cx, short *cy, int cell, int dir);

// ---- raw same-cluster callees ----
int    FUN_8035403c(int idx, int source);         // short setting lookup (this file)
int    FUN_803540b0(int idx);                      // editor setting resolve (this file)
void   FUN_803541d0(int rec, int rowCount, int firstCell, uint tmpl); // grid rotate/scan
int    FUN_803542c0(int templateIdx);              // template referenced (this file)
int    FUN_803543ec(void);                         // check current template (this file)
int    FUN_803826e4(void);                         // current selected template id (-1 if none)
undefined8 FUN_803826e8(void);                     // enter editor-source ctx -> (hi=rec, lo=cell)
void   FUN_80382730(void);                         // leave editor id ctx
void   FUN_80382734(void);                         // leave editor-source ctx

// ---- raw globals / tables ----
extern int  DAT_80612be8;    // active challenge/mode record base
extern int  DAT_80612ae8;    // "use loadout settings" flag
extern char DAT_805015bd;    // co-op / game mode selector
extern undefined DAT_8047c148;    // template dims table base (byte-addressed, stride 0x140)
extern undefined DAT_8047c164;    // template cell-count field base (byte-addressed, stride 0x140)
extern undefined DAT_8048f97c;    // per-type layout descriptor table base (byte-addressed, stride 0xa4)

// ============================================================================
// 0x8035403c  challenge_lookup_setting_short
// Short-valued setting fetch: source 5 reads the challenge record inline table
// (element param_1 +6 shorts); other sources read the game settings block
// (+0x2d0 + source*0xa4 + idx*8).
// ============================================================================
int challenge_lookup_setting_short(int param_1, int param_2)
{
    short      v;
    int       *section;
    undefined *settings;

    if (param_2 == 5) {
        section = challenge_record_find_section(DAT_80612be8, 0xc77668);
        v = *(short *)(section + param_1 + 6);
    } else {
        settings = game_get_settings_ptr();
        v = *(short *)(settings + param_2 * 0xa4 + param_1 * 8 + 0x2d0);
    }
    return (int)v;
}

// ============================================================================
// 0x803540b0  challenge_resolve_editor_setting
// Resolve editor setting[param_1] across sources. Without loadout settings the
// source is the challenge record mode id (+5 short): 0 => -1, <0xc reads the
// static default table (0x80490acc), else defers to the short lookup. With
// loadout settings the source is game settings +0x6a8 (or the challenge mode id
// in co-op '\n'): <0xc reads the resolved menu option table, else defers.
// ============================================================================
int challenge_resolve_editor_setting(int param_1)
{
    int       *section;
    undefined *settings;
    int        val;

    section = challenge_record_find_section(DAT_80612be8, 0xc77668);
    if (DAT_80612ae8 == 0) {
        val = (int)*(short *)(section + 5);
        if (val == 0) {
            val = -1;
        } else if (val < 0xc) {
            val = (int)*(short *)(val * 0xa4 + param_1 * 8 + -0x7fb6f534);
        } else {
            val = FUN_8035403c(param_1, val + -0xc);
        }
    } else {
        if (DAT_805015bd == '\n') {
            val = (int)*(short *)(section + 5);
        } else {
            settings = game_get_settings_ptr();
            val = *(int *)(settings + 0x6a8);
        }
        if (val == 0) {
            val = -1;
        } else if (val < 0xc) {
            section = menu_slot_option_table_resolve(val);
            val = (int)*(short *)(section + param_1 * 2 + 1);
        } else {
            val = FUN_8035403c(param_1, val + -0xc);
        }
    }
    return val;
}

// ============================================================================
// 0x803541a0  challenge_get_editor_setting
// Resolve the editor setting for the entity's option index (+0x24 ushort) and
// fold the two's-complement result into a normalized flag.
// ============================================================================
int challenge_get_editor_setting(int param_1)
{
    int v;
    int t;

    v = FUN_803540b0((int)(uint)*(ushort *)(param_1 + 0x24));
    t = -v + -1;
    return t - ((int)(uint)(t == 0) + -v + -2);
}

// ============================================================================
// 0x803541d0  aveditor_rotate_grid_scan
// Rotate a template's cell grid (rowCount rows of 0x140 bytes copied into a
// local scratch grid) by calling aveditor_rotate_cell per row, then walk the
// rotated footprint column against the editor grid (tmpl +0x20 + col*4 pointer
// table, tmpl +0x14 stride, tmpl +0x1c row count) looking for a blocked cell
// (bit 0x1000). param_1 is the source record (fields +0x15/+0x16/+0x17 seed the
// rotate origin); param_2 is the starting column index.
// ============================================================================
void FUN_803541d0(int param_1, int param_2, int param_3, uint param_4)
{
    int         col;
    int         remaining;
    undefined1 *rowPtr;
    int         rowOff;
    short       ox, oy, oz;
    undefined1  grid[1312];

    aveditor_rotate_template_dims((uint)(unsigned long long)grid, param_4);
    rowOff = param_3 * 0x140;
    ox = (short)(ushort)*(byte *)(param_1 + 0x15);
    oy = (short)(ushort)*(byte *)(param_1 + 0x16);
    oz = (short)(ushort)*(byte *)(param_1 + 0x17);
    for (; param_3 != 0; param_3 = param_3 + -1) {
        rowPtr = grid + rowOff;
        rowOff = rowOff + -0x140;
        aveditor_rotate_cell(&ox, &oy, (int)rowPtr, -1);
    }
    col       = param_2 << 2;
    remaining = *(int *)(param_4 + 0x1c) - param_2;
    if (param_2 < *(int *)(param_4 + 0x1c)) {
        do {
            if ((*(uint *)((((int)ox + (int)oy * *(int *)(param_4 + 0x14)) * 4) +
                           *(int *)(param_4 + col + 0x20)) & 0x1000) != 0) break;
            col       = col + 4;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
    (void)oz;
}

// ============================================================================
// 0x803542c0  challenge_template_referenced
// Scan the challenge record's 0xc7766e reference sections (respecting the co-op
// mode gating on section byte +0xc) for a template index equal to param_1, in
// either the first ref list (+0x5c, count at [+0x16]) or the second (+0x94,
// count at [+0x24]). Returns 1 on the first match, else 0.
// ============================================================================
undefined4 challenge_template_referenced(int param_1)
{
    int   count;
    int  *section;
    int   off;
    int  *cur;

    cur     = (int *)(DAT_80612be8 + 2);
    section = challenge_record_find_section(DAT_80612be8, 0xc77668);
    do {
        if ((int *)((int)DAT_80612be8 + section[0x8f]) <= cur) {
            return 0;
        }
        if (((*cur == 0xc7766e) && ((DAT_805015bd != '\n' || (*(char *)(cur + 3) == '\0')))) &&
            ((DAT_805015bd != '\x03' || (*(char *)(cur + 3) == '\x01')))) {
            count = cur[0x16];
            off   = 0;
            if (0 < count) {
                do {
                    if (*(int *)((int)cur + off + 0x5c) == param_1) { return 1; }
                    off   = off + 4;
                    count = count + -1;
                } while (count != 0);
            }
            count = cur[0x24];
            off   = 0;
            if (0 < count) {
                do {
                    if (*(int *)((int)cur + off + 0x94) == param_1) { return 1; }
                    off   = off + 4;
                    count = count + -1;
                } while (count != 0);
            }
        }
        cur = (int *)((int)cur + cur[1] + 8);
    } while (true);
}

// ============================================================================
// 0x803543ec  challenge_check_current_template
// For the currently selected template id (FUN_803826e4), walk the 0xc7766d id
// sections and, when one matches the selected id, test whether its running index
// is referenced (challenge_template_referenced). Enters/leaves the editor id ctx.
// ============================================================================
void challenge_check_current_template(void)
{
    int   selId;
    int  *section;
    int   idx;
    int   ref;
    int  *cur;
    int  *end;

    selId = FUN_803826e4();
    if (selId != -1) {
        cur     = (int *)(DAT_80612be8 + 2);
        section = challenge_record_find_section(DAT_80612be8, 0xc77668);
        idx     = 0;
        end     = (int *)((int)DAT_80612be8 + section[0x8f]);
        for (; cur < end; cur = (int *)((int)cur + cur[1] + 8)) {
            if (*cur == 0xc7766d) {
                if ((*(short *)(cur + 5) == selId) && (ref = FUN_803542c0(idx), ref != 0)) break;
                idx = idx + 1;
            }
        }
    }
    FUN_80382730();
}

// ============================================================================
// 0x803544a8  aveditor_tick_rotate_template
// Per-frame editor tick for a placed template entity (class +0xe in 0x35..0x37).
// Computes the placement column from the cell byte (+6) plus the entity sub-cell
// (+0xd), and if the placement is not already referenced by the current template
// (co-op gating via challenge_check_current_template) and fits within the
// template's dims, rotates/scans its grid (FUN_803541d0).
// ============================================================================
void aveditor_tick_rotate_template(void)
{
    short  cls;
    uint   clz;
    int    ref;
    int    col;
    int   *rec;
    int   *matchRec;
    int    lastCol;
    int    baseCol;
    undefined8 ctx;

    ctx      = FUN_803826e8();
    rec      = (int *)((ulonglong)ctx >> 0x20);
    matchRec = (int *)ctx;   // reused as the cell record
    cls      = *(short *)((int)rec + 0xe);
    if ((((cls != 0x35) && (0x34 < cls)) && (cls < 0x38)) &&
        (((baseCol = (int)*(char *)((int)matchRec + 6) + (uint)*(byte *)((int)rec + 0xd),
           DAT_805015bd != '\n' && (DAT_805015bd != '\x03')) ||
          (ref = FUN_803543ec(), ref == 0)))) {
        int *sel = matchRec;
        if (*(char *)((int)rec + 0xd) != '\0') {
            sel = challenge_record_find_match(matchRec, DAT_80612be8, baseCol);
        }
        lastCol = *(int *)(&DAT_8047c164 + *sel * 0x140) + (int)*(char *)((int)sel + 6) + -1;
        col     = lastCol - baseCol;
        clz     = countLeadingZeros((uint)col);
        if ((((sel[2] & 0x2000000U) == 0) && (clz >> 5 == 0)) && (baseCol < lastCol)) {
            FUN_803541d0((int)rec, col, (int)*(char *)((int)matchRec + 7),
                         (uint)(unsigned long)(&DAT_8047c148 + *sel * 0x140));
        }
    }
    FUN_80382734();
}

// ============================================================================
// 0x803545dc  arcsel_entity_layout_desc
// Return the arcade-select layout descriptor for an entity: type 0x11 uses the
// entity's own descriptor (+0x150 -> +0xe0), otherwise the static table
// DAT_8048f97c indexed by (type + 0x1b) with stride 0xa4.
// ============================================================================
undefined *arcsel_entity_layout_desc(int param_1)
{
    int type;

    type = *(int *)(param_1 + 8);
    if ((0xb < type) && (type == 0x11)) {
        return (undefined *)(*(int *)(param_1 + 0x150) + 0xe0);
    }
    return &DAT_8048f97c + (type + 0x1b) * 0xa4;
}

// ============================================================================
// 0x80354618  arcsel_copy_short_list
// Copy a count-prefixed short list: param_2[0] is the element count (byte), the
// shorts follow at +2; write the count into param_1[0] and each short at
// param_1 +4 + i*4.
// ============================================================================
void arcsel_copy_short_list(uint *param_1, byte *param_2)
{
    int srcOff;
    int dstOff;

    dstOff = 0;
    srcOff = 0;
    *param_1 = (uint)*param_2;
    for (int i = 0; i < (int)(uint)*param_2; i = i + 1) {
        int s = srcOff + 2;
        int d = dstOff + 4;
        dstOff = dstOff + 8;
        srcOff = srcOff + 4;
        *(undefined2 *)((int)param_1 + d) = *(undefined2 *)(param_2 + s);
    }
}

// ============================================================================
// 0x8035465c  arcsel_validate_placement
// Validate a placement: fetch the layout descriptor for the entity option
// (+0x24 ushort +0x38); if its validate callback (+0x14) returns 0, or the
// entity's match-mode byte (+0x20) is not type 5, the placement is invalid (0).
// ============================================================================
undefined4 arcsel_validate_placement(int param_1)
{
    undefined *desc;
    int        cb;
    uint       type5;
    undefined4 ok;

    ok   = 1;
    desc = arcsel_get_layout_desc((int)*(ushort *)(param_1 + 0x24) + 0x38);
    if (((*(code **)(desc + 0x14) != (code *)0) &&
         (cb = ((int (*)(int, int))*(code **)(desc + 0x14))(param_1, 0), cb == 0)) ||
        (type5 = match_mode_is_type5((int)*(char *)(param_1 + 0x20)), type5 == 0)) {
        ok = 0;
    }
    return ok;
}

} // extern "C"
