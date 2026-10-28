// Batch: batch_960 | Module: arcade/challenge editor custom-map record container parsing + buffer
// Address range: 0x80339b90 - 0x80339f20
// Date: 2026-09-12
//
// The container format for the arcade/challenge custom-map "record" blob: a header (magic
// 0xc77667, version 0x12d) followed by a chain of variable-length tagged sections. Each section
// starts with a tag word (+0) and a byte-length word (+4); the next section is at
// (section + 8 + length). The terminator section carries tag 0xc7766a. This TU provides the
// section lookup used across the whole editor/menu/weapon/scene code (find_section is one of the
// most-called helpers in the build), plus the entry-iteration walk over the roster sections
// (0xc77668 count section + 0xc77669 data section), an entry-match predicate, and the
// alloc/clear/size helpers for the global record buffer at DAT_80612d58.
//
//   challenge_record_find_section     0x80339b90  find a section by tag id (validates header magic).
//   challenge_record_find_end         0x80339c00  find the terminator section (tag 0xc7766a).
//   challenge_record_flag_is_set      0x80339c28  1 when the global record's 0xc77668 flag byte set.
//   challenge_record_iterate_entries  0x80339c7c  walk the roster entries of a record.
//   challenge_record_entry_matches    0x80339d38  entry-vs-entry match predicate (fields + DAT tbl).
//   challenge_record_find_match       0x80339ddc  walk roster entries, stop at first match.
//   challenge_record_buffer_size      0x80339eb4  return the record buffer size (const 0x1c80).
//   challenge_record_buffer_size_alt  0x80339ebc  thunk to challenge_record_buffer_size.
//   challenge_record_buffer_clear     0x80339edc  memset the global record buffer + zero two words.
//   challenge_record_buffer_alloc     0x80339f20  allocate the global record buffer (size by mode).
//
// NON-MATCHING: challenge_record_iterate_entries / challenge_record_find_match read their record
// pointer from an incoming register that Ghidra modelled as a _savegpr_ spill; per the Compile-Clean
// standard that value is expressed as the real `record` parameter (rules 1 & 3). The section length
// field is a byte at +5 within roster entries; the roster header stride is *(short*)(section+9)*4
// via the +0x24 short and the +0x1e entry-count short - reproduced from the pointer arithmetic.
// challenge_record_buffer_alloc's allocator takes (size, type); Ghidra showed the varargs float/GPR
// pass-through noise, collapsed here to the real 2-arg mem_alloc(size, 1) (declared file-locally).
// The record buffer entry stride used by the roster walk (+0x18 struct, +5 byte length) is read
// straight from the section payload; the +0x1e / +0x24 shorts index the sub-entry table.

#include "common.h"

extern "C" {

extern int DAT_80612be8;    // global streamed record buffer (also the parse target)
extern int DAT_80612614;    // build/mode selector (0x66 => alternate buffer size path)
extern int DAT_80612d58;    // global record buffer base (allocated by challenge_record_buffer_alloc)

extern int DAT_8047c15c;    // per-slot descriptor table: field compared for entry match (+0x140 stride)
extern int DAT_8047c160;    // per-slot descriptor table: second compared field
extern int DAT_8047c164;    // per-slot descriptor table: range-count field

// common.h pulls in generated_externs.h but not generated_decls.h, so the real record-heap
// allocator and memset wrapper are declared file-locally (signatures match generated_decls.h).
extern int mem_alloc(int size, int type);
extern int memset_wrapper(int dst, int value, int len);

} // extern "C"

// Record container constants.
enum {
    CHALLENGE_RECORD_MAGIC   = 0xc77667,   // header word 0
    CHALLENGE_RECORD_VERSION = 0x12d,      // header word 1
    CHALLENGE_SECTION_END    = 0xc7766a,   // terminator section tag
    CHALLENGE_SECTION_FLAGS  = 0xc77668,   // roster count / flags section tag
    CHALLENGE_SECTION_ROSTER = 0xc77669,   // roster data section tag
    CHALLENGE_RECORD_LIMIT   = 0x1fff,     // max words scanned from the record base
    CHALLENGE_BUFFER_SIZE    = 0x1c80,     // default record buffer size
};

// -----------------------------------------------------------------------------

// Find the section tagged `tag` inside a record. Validates the header (magic 0xc77667, version
// 0x12d) then walks the section chain: each section has [tag, byteLen, payload...] and the next
// starts at (section + 8 + byteLen). Returns the payload pointer (section + 8), or null if the
// header is invalid, the terminator (0xc7766a) is reached, or the scan limit is exceeded.
int* challenge_record_find_section(int* record, int tag)
{
    if (record[0] != CHALLENGE_RECORD_MAGIC || record[1] != CHALLENGE_RECORD_VERSION) {
        return (int*)0;
    }
    int* section = record + 2;
    while (section < record + CHALLENGE_RECORD_LIMIT) {
        if (section[0] == tag) {
            return section + 2;
        }
        if (section[0] == CHALLENGE_SECTION_END) {
            return (int*)0;
        }
        section = (int*)((int)(section + 2) + section[1]);
    }
    return (int*)0;
}

// Locate the record terminator section (tag 0xc7766a). Used to find the append point / end marker.
void challenge_record_find_end(int* record)
{
    challenge_record_find_section(record, CHALLENGE_SECTION_END);
}

// Return 1 when the global record's flags section (tag 0xc77668) is present and its first byte is
// non-zero, i.e. the record has the queried capability flag set.
int challenge_record_flag_is_set(void)
{
    if (DAT_80612be8 == 0) {
        return 0;
    }
    unsigned* flags = (unsigned*)challenge_record_find_section((int*)(intptr_t)DAT_80612be8,
                                                               CHALLENGE_SECTION_FLAGS);
    if ((*flags & 0xff) != 0) {
        return 1;
    }
    return 0;
}

// Walk the roster of a record: the flags section (0xc77668) payload +4 holds the row count; the
// roster data section (0xc77669) is a table of rows. Each row header is at +0x24 (short stride *4
// added via *(short*)(row+9)*4... reproduced as byte arithmetic), and holds a sub-entry count at
// +0x1e; each sub-entry advances by (*(byte*)(entry+0x14) + 6) words. This is the traversal used to
// size / validate the roster; it produces no output here (the original consumes the walked pointer).
void challenge_record_iterate_entries(int* record)
{
    int* row   = challenge_record_find_section(record, CHALLENGE_SECTION_ROSTER);
    int* flags = challenge_record_find_section(record, CHALLENGE_SECTION_FLAGS);
    int rowCount = flags[1];
    for (int i = 0; i < rowCount; i++) {
        unsigned subCount = (unsigned)*(unsigned short*)((int)row + 0x1e);
        row = (int*)((int)row + (int)*(short*)(row + 9));
        for (; subCount != 0; subCount--) {
            row = row + *(unsigned char*)(row + 5) + 6;
        }
    }
}

// Match predicate for a roster entry against a reference entry. Both entries index the per-slot
// descriptor table (0x140 stride at &DAT_8047c15c/160/164 keyed by entry[0]); the entry matches when
// its slot index (+6 byte) is within the reference's range, its +1 and +5 bytes equal the
// reference's, and both descriptor fields (+0x8047c15c/+0x8047c160) match. Returns 1 on match.
int challenge_record_entry_matches(int* reference, int* entry, int slot)
{
    int off = entry[0] * 0x140;
    if (*(char*)((int)entry + 6) <= slot &&
        slot <= *(int*)(&DAT_8047c164 + off) + (int)*(char*)((int)entry + 6) - 1 &&
        *(char*)(entry + 1) == *(char*)(reference + 1) &&
        *(char*)((int)entry + 5) == *(char*)((int)reference + 5) &&
        *(int*)(&DAT_8047c15c + off) == *(int*)(&DAT_8047c15c + reference[0] * 0x140) &&
        *(int*)(&DAT_8047c160 + off) == *(int*)(&DAT_8047c160 + reference[0] * 0x140)) {
        return 1;
    }
    return 0;
}

// Walk the record's roster (as challenge_record_iterate_entries) but stop at the first row whose
// entry matches the reference (via challenge_record_entry_matches) for the given slot.
void challenge_record_find_match(int* record, int slot)
{
    int* row   = challenge_record_find_section(record, CHALLENGE_SECTION_ROSTER);
    int* flags = challenge_record_find_section(record, CHALLENGE_SECTION_FLAGS);
    int rowCount = flags[1];
    for (int i = 0; i < rowCount; i++) {
        int* next = (int*)((int)row + (int)*(short*)(row + 9));
        if (challenge_record_entry_matches(record, row, slot) != 0) {
            break;
        }
        unsigned subCount = (unsigned)*(unsigned short*)((int)row + 0x1e);
        for (; subCount != 0; subCount--) {
            next = next + *(unsigned char*)(next + 5) + 6;
        }
        row = next;
    }
}

// The default record buffer size.
int challenge_record_buffer_size(void)
{
    return CHALLENGE_BUFFER_SIZE;
}

// Thunk to challenge_record_buffer_size (used on the alternate build/mode path).
int challenge_record_buffer_size_alt(void)
{
    return challenge_record_buffer_size();
}

// Clear the global record buffer to its full size and zero the two header words at +4 / +8.
void challenge_record_buffer_clear(void)
{
    unsigned size = (unsigned)challenge_record_buffer_size();
    memset_wrapper(DAT_80612d58, 0, size);
    *(int*)(DAT_80612d58 + 4) = 0;
    *(int*)(DAT_80612d58 + 8) = 0;
}

// Allocate the global record buffer (size selected by the build/mode flag DAT_80612614==0x66) and
// clear it. The allocator is the shared mem_alloc(size, type); type 1 is the record heap.
void challenge_record_buffer_alloc(void)
{
    int size;
    if (DAT_80612614 == 0x66) {
        size = challenge_record_buffer_size_alt();
    } else {
        size = challenge_record_buffer_size();
    }
    DAT_80612d58 = mem_alloc(size, 1);
    challenge_record_buffer_clear();
}
