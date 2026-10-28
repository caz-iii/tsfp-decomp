// Batch: batch_956 | Module: arcade map-editor participant (bot) placement + limits
// Address range: 0x80338528 - 0x80338934
// Date: 2026-09-12
//
// The participant/bot placement layer of the arcade match editor. Sits on top of the
// participant pool (aveditor_create_participant) and the placed-cell registry
// (aveditor_register_placed_cell). Adding a participant allocates an objective cell, links it into
// the current node's placed-cell list, formats its two entry labels, and bumps the participant
// count (+0x41d8). A parallel limit check (+0x41d8 >= 10 participants OR +0x41e0 >= 30 nodes) gates
// placement and drives the "too many" dialog. The editor context block is DAT_80612be4; the
// participant array is a 0x220-byte-stride table indexed off it, the placed-cell lists live at
// +0x81a8 (0xb4 stride), and the roster/objective descriptors at +0xdd14 / +0xdd90 (0x8c stride).
//
//   arcade_edit_at_participant_limit  0x80338528  1 when at/over participant(10) or node(30) limit.
//   arcade_edit_show_limit_dialog     0x80338558  show the "max participants" info + back dialog.
//   arcade_edit_cell_set_flag_21c     0x803385f0  set +0x21c=1 on a cell record.
//   arcade_edit_label_placed_cell     0x803385fc  mark placed-cell +0xa8=1 and format its label.
//   arcade_edit_slot_is_empty         0x80338630  -1 when the participant slot has no bots/objectives.
//   arcade_edit_slot_bot_sign         0x80338668  clamped sign of the slot's bot count (+0x426c).
//   arcade_edit_back_key              0x80338690  back/exit key handler: pick confirm-screen id.
//   arcade_edit_set_cell_style        0x80338740  set a placed-cell render style by kind + render it.
//   arcade_edit_add_participant       0x803387f0  add a bot participant: alloc cell, register, label.
//   arcade_edit_place_toolbar_item    0x80338934  place the pending toolbar item at the current node.
//
// NON-MATCHING: arcade_edit_add_participant / arcade_edit_place_toolbar_item are emitted by the
// decompiler carrying the 8 register-pair (r3-r10) pass-through slots the caller forwards to the
// label formatter; those are expressed here as the explicit `ctx`-style args the ABI actually uses
// plus opaque forwarded registers (Compile-Clean rules 1/3). The compiler int->float magic and the
// countLeadingZeros idiom in the limit check are written as a plain boolean. Raw FUN_/DAT_ symbols
// get per-file externs below; named aveditor_*/match_*/hud_*/screen_* helpers resolve through the
// shared headers (generated_decls.h via hud.h).

#include "common.h"
// NB: this TU deliberately does NOT include "hud.h". The shared generated_decls.h declares several
// of the aveditor_* helpers with a `void` return (their IDB prototype was never given a return
// type), but the real ABI returns a value that every call site here uses. Following the b949
// precedent (arcade_edit_entry_render.cpp), we include only common.h and declare the helpers this
// file calls file-locally with their correct int returns, avoiding a C2371 clash with the void
// shared decls.

extern "C" {

// editor context block: participant count at +0x41d8, node count at +0x41e0, screen-id at +0x20c,
// dirty flag at +0x254, roster table at +0xdd14, objective-descriptor table at +0xdd90 (0x8c stride),
// placed-cell lists at +0x81a8 (0xb4 stride), pending toolbar item ptr at +0x11858/+0x1185c.
extern int DAT_80612be4;
extern int DAT_80612d0c;   // global editor mode/state flag

// pending-render vtable: entry-label render thunks, 0x13-int (0x4c-byte) stride, indexed by kind.
extern int PTR_match_render_entry_label_80480f44[];

// raw FUN_ helpers not yet named
extern void FUN_80387fb8(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                         unsigned r7, unsigned r8, unsigned r9, unsigned r10,
                         int ctx, const char* fmt, int count,
                         unsigned a12, unsigned a13, unsigned a14, unsigned a15, unsigned a16); // number-into-string formatter
extern void FUN_80348238(int kind);   // mark editor subsystem dirty / request rebuild
extern void FUN_80338a8c(void);        // post-place refresh of the current node's cell list

// named cross-module helpers (declared here, not via hud.h, so returns can be typed correctly)
extern int  get_string_by_id(int id);
extern void hud_create_back_dialog(int unused_a, int unused_b, int user_data);
extern void screen_exit_dirty_check(void);
extern void match_format_entry_label(unsigned long long f1, unsigned long long a2,
                                     unsigned long long a3, unsigned long long a4,
                                     unsigned long long a5, unsigned long long a6,
                                     unsigned long long a7, unsigned long long a8,
                                     int* entry, int cellBase, unsigned a11, unsigned a12,
                                     unsigned a13, unsigned a14, unsigned a15, unsigned a16);
extern void aveditor_register_placed_cell(int slot, int* cell);
extern int  aveditor_create_participant(unsigned long long p1, unsigned long long p2,
                                        unsigned long long p3, unsigned long long p4,
                                        unsigned long long p5, unsigned long long p6,
                                        unsigned long long p7, unsigned long long p8);
extern int  aveditor_alloc_cell(unsigned long long f1, unsigned long long a2,
                                unsigned long long a3, unsigned long long a4,
                                unsigned long long a5, unsigned long long a6,
                                unsigned long long a7, unsigned long long a8);
extern int  aveditor_create_objective_cell(unsigned type, unsigned a2,
                                           unsigned short label, unsigned short idx);

} // extern "C"

// forward prototypes for this file's helpers referenced before their definition
int  arcade_edit_cell_set_flag_21c(int cell);
void arcade_edit_label_placed_cell(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                                   unsigned r7, unsigned r8, unsigned r9, unsigned r10,
                                   int* placedCell, int ctx, int cellId,
                                   unsigned a11, unsigned a12, unsigned a13,
                                   unsigned a14, unsigned a15);
void arcade_edit_set_cell_style(int placedCell, int kind);

// -----------------------------------------------------------------------------

// Return 1 when the editor is at or over its placement limits (10 participants or 30 nodes).
int arcade_edit_at_participant_limit(void)
{
    return (*(int*)(DAT_80612be4 + 0x41d8) > 9 ||
            *(int*)(DAT_80612be4 + 0x41e0) > 0x1d) ? 1 : 0;
}

// Show the "maximum participants" info text (string 0xb56, with the current count) then the back
// dialog. When under the participant limit the count line is only shown once the node limit is hit.
void arcade_edit_show_limit_dialog(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                                   unsigned r7, unsigned r8, unsigned r9, unsigned r10,
                                   int dialogParent, unsigned a10, unsigned a11, unsigned a12,
                                   unsigned a13, unsigned a14, unsigned a15, unsigned a16)
{
    int count = *(int*)(DAT_80612be4 + 0x41d8);
    if (count < 10) {
        if (*(int*)(DAT_80612be4 + 0x41e0) > 0x1d) {
            const char* fmt = (const char*)get_string_by_id(0xb56);
            FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, DAT_80612be4, fmt, count - 1,
                         a12, a13, a14, a15, a16);
        }
    } else {
        const char* fmt = (const char*)get_string_by_id(0xb56);
        FUN_80387fb8(r3, r4, r5, r6, r7, r8, r9, r10, DAT_80612be4, fmt, 10,
                     a12, a13, a14, a15, a16);
    }
    hud_create_back_dialog(dialogParent, DAT_80612be4, 0);
}

// Set the "placed" flag (+0x21c=1) on a cell record. Returns the record for call chaining.
int arcade_edit_cell_set_flag_21c(int cell)
{
    *(int*)(cell + 0x21c) = 1;
    return cell;
}

// Mark a placed-cell record active (+0xa8=1) and (re)format its entry label from the roster table.
void arcade_edit_label_placed_cell(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                                   unsigned r7, unsigned r8, unsigned r9, unsigned r10,
                                   int* placedCell, int ctx, int cellId,
                                   unsigned a11, unsigned a12, unsigned a13,
                                   unsigned a14, unsigned a15)
{
    placedCell[0x2a] = 1;   // +0xa8
    match_format_entry_label(r3, r4, r5, r6, r7, r8, r9, r10, placedCell,
                             DAT_80612be4 + 0xdd14, a11, a12, a13, a14, a15, cellId);
}

// -1 when the participant slot (index) is empty: no objectives (+0x426c < 1) or no bots (+0x4234 < 1).
int arcade_edit_slot_is_empty(int index)
{
    int slot = DAT_80612be4 + index * 0x220;
    bool empty = (*(int*)(slot + 0x426c) < 1) || (*(int*)(slot + 0x4234) < 1);
    return empty ? -1 : -2;
}

// Clamped sign of the slot's bot/objective count (+0x426c): -1, 0, or 1.
int arcade_edit_slot_bot_sign(int index)
{
    int n = *(int*)(DAT_80612be4 + index * 0x220 + 0x426c);
    return (n >> 0x1f) + (n != 0 ? 1 : 0);
}

// Back/exit key handler for the participant editor: choose the confirm-screen id (+0x20c) from the
// current sub-mode (+0x188), mark the screen dirty (+0x254=1), then run the exit dirty check.
void arcade_edit_back_key(int screen)
{
    int mode = *(int*)(screen + 0x188);
    if (mode == 1) {
        *(int*)(DAT_80612be4 + 0x20c) = 0x1b;
        *(int*)(DAT_80612be4 + 0x254) = 1;
    } else if (mode < 1) {
        if (mode >= 0) {   // mode == 0
            *(int*)(DAT_80612be4 + 0x20c) = 0x22;
            *(int*)(DAT_80612be4 + 0x254) = 1;
        }
    } else if (mode < 3) { // mode == 2
        *(int*)(DAT_80612be4 + 0x20c) = 0x1c;
        *(int*)(DAT_80612be4 + 0x254) = 1;
    }
    DAT_80612d0c = 2;
    *(int*)(DAT_80612be4 + 0x1185c) = 8;
    screen_exit_dirty_check();
}

// Set a placed-cell's render style (+8 = style id, +4 = render-kind index) by the item kind, then
// dispatch its render thunk from the render-entry-label vtable.
void arcade_edit_set_cell_style(int placedCell, int kind)
{
    if (kind == 0x37) {
        *(int*)(placedCell + 8) = 0x18;
        *(int*)(placedCell + 4) = 0;
    } else if (kind < 0x37) {
        if (kind > 0x34) {
            *(int*)(placedCell + 8) = 0x18;
            *(int*)(placedCell + 4) = 0;
        }
    } else if (kind == 0x46) {
        *(int*)(placedCell + 8) = 0x15;
        *(int*)(placedCell + 4) = 0xb;
    }
    *(int*)(placedCell + 0xa8) = 1;
    typedef void (*RenderThunk)(int, int, int);
    int idx = *(int*)(placedCell + 4);
    RenderThunk thunk = (RenderThunk)(intptr_t)PTR_match_render_entry_label_80480f44[idx * 0x13];
    thunk(placedCell, DAT_80612be4 + 0xdd14, DAT_80612be4 + 0x288);
}

// Add a bot participant to the current node when under the participant limit: create the participant
// record, allocate/reuse the shared objective cell (+0x2f1c list, slot +4), mark its roster
// descriptor active (+0xdd90), register an objective cell, bump the participant count (+0x41d8),
// then format the label for both this node's and its linked node's placed-cell lists.
void arcade_edit_add_participant(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                                 unsigned r7, unsigned r8, unsigned r9, unsigned r10)
{
    if (*(int*)(DAT_80612be4 + 0x41d8) >= 10) {
        return;
    }
    *(int*)(DAT_80612be4 + 0x248) = 0;

    int participant = aveditor_create_participant(r3, r4, r5, r6, r7, r8, r9, r10);
    int cellId = *(int*)(*(int*)(DAT_80612be4 + 0x2f1c) + 4);
    int rosterSlot = *(int*)(DAT_80612be4 + *(int*)(participant + 0xa4) * 0x220 + 0x41e4);

    if (cellId < 0) {
        cellId = aveditor_alloc_cell(0, r4, r5, r6, r7, r8, r9, r10);
        *(int*)(*(int*)(DAT_80612be4 + 0x2f1c) + 4) = cellId;
    }
    *(int*)(DAT_80612be4 + cellId * 0x8c + 0xdd90) = 1;

    int cell = aveditor_create_objective_cell(0, 4, (unsigned short)cellId, 0xffff);
    int slotBase = rosterSlot * 0xb4;
    aveditor_register_placed_cell(DAT_80612be4 + slotBase + 0x81a8, (int*)(intptr_t)cell);
    *(int*)(DAT_80612be4 + 0x41d8) = *(int*)(DAT_80612be4 + 0x41d8) + 1;

    int rec = arcade_edit_cell_set_flag_21c(
        DAT_80612be4 + *(int*)(participant + 0xa4) * 0x220 + 0x41e4);
    (void)rec;

    // label this node's placed-cell list
    arcade_edit_label_placed_cell(r3, r4, r5, r6, r7, r8, r9, r10,
                                  (int*)(intptr_t)(DAT_80612be4 + slotBase + 0x81a8),
                                  DAT_80612be4, cellId, 0, 0, 0, 0, 0);
    // and the linked node's list (chained through +0x824c -> +0x4238)
    int linked = *(int*)(DAT_80612be4 + *(int*)(DAT_80612be4 + slotBase + 0x824c) * 0x220 + 0x4238);
    arcade_edit_label_placed_cell(r3, r4, r5, r6, r7, r8, r9, r10,
                                  (int*)(intptr_t)(DAT_80612be4 + linked * 0xb4 + 0x81a8),
                                  DAT_80612be4, cellId, 0, 0, 0, 0, 0);
}

// Place the pending toolbar item (ptr at +0x11858) into the map at the current node when that node
// has no objectives yet (+0x404c < 1): assign its roster owner (+0xa4), set its render style,
// register it into the node's placed-cell list, label it, request a rebuild of the objective
// subsystem the first time (+0x238 & 0x20), then refresh the node.
void arcade_edit_place_toolbar_item(unsigned r3, unsigned r4, unsigned r5, unsigned r6,
                                    unsigned r7, unsigned r8, unsigned r9, unsigned r10)
{
    int item = *(int*)(DAT_80612be4 + 0x11858);
    if (item == 0) {
        return;
    }
    int cellId = (int)*(short*)(item + 0xc);
    int node = DAT_80612be4 + *(int*)(DAT_80612be4 + 0x41e0) * 0x220;
    if (*(int*)(node + 0x404c) < 1) {
        int slotBase = *(int*)(node + 0x3fc4) * 0xb4;
        *(int*)(item + 0xa4) = *(int*)(DAT_80612be4 + slotBase + 0x824c);
        arcade_edit_set_cell_style(*(int*)(DAT_80612be4 + 0x11858),
                                   *(int*)(DAT_80612be4 + cellId * 0x8c + 0xdd98));
        aveditor_register_placed_cell(DAT_80612be4 + slotBase + 0x81a8,
                                      *(int**)(DAT_80612be4 + 0x11858));

        int linked = *(int*)(DAT_80612be4 + slotBase + 0x824c);
        int list = *(int*)(DAT_80612be4 + linked * 0x220 + 0x4270) * 0xb4;
        arcade_edit_label_placed_cell(r3, r4, r5, r6, r7, r8, r9, r10,
                                      (int*)(intptr_t)(DAT_80612be4 + list + 0x81a8),
                                      DAT_80612be4, cellId, 0, 0, 0, 0, 0);

        if ((*(unsigned*)(DAT_80612be4 + 0x238) & 0x20) == 0) {
            FUN_80348238(3);
            *(unsigned*)(DAT_80612be4 + 0x238) |= 0x20;
        }
    }
    FUN_80338a8c();
}
