// Address: 0x8002ba24-0x8002cf40 | Name: inflate | Date: 2026-08-05
// DEFLATE/gzip decompression implementation (RFC 1951 / RFC 1952).
// Used by the pack file system to decompress compressed .pak entries.
// This is a custom implementation (not zlib) optimized for GameCube memory.
//
// Supports all three DEFLATE block types:
//   Type 0: Stored (uncompressed) blocks
//   Type 1: Fixed Huffman codes (predefined code lengths)
//   Type 2: Dynamic Huffman codes (code lengths in stream)
//
// Uses a 32KB sliding window (0x8000 bytes) with flush-on-full semantics.
// Huffman tables are built using multi-level lookup tables for fast decoding.
//
// NOTE: For the PC port, this can be replaced with zlib/miniz inflate.
// The compressed pack entries use standard gzip format (magic 0x1f 0x8b).

#include "common.h"
#include "inflate.h"
#include "mem_alloc.h"

// Constants
#define INFLATE_WINDOW_SIZE  0x8000  // 32KB sliding window
#define INFLATE_MAX_BITS     16
#define INFLATE_FIXED_LIT    288     // 0x120 fixed literal/length codes
#define INFLATE_FIXED_DIST   30      // 0x1E fixed distance codes

// Inflate state globals:
// DAT_80611888 = page buffer base pointer
// DAT_8061188c = bit buffer count (bits available)
// DAT_80611890 = bit buffer (accumulated bits)
// DAT_80611894 = output destination base
// DAT_80611898 = total bytes written to output
// DAT_8061189c = window write position
// DAT_806118a0 = window buffer pointer (32KB)
// DAT_806118a4 = input size (total compressed bytes)
// DAT_806118a8 = input read position
// DAT_806118ac = input buffer pointer
// DAT_806118b0 = current output write pointer

// ============================================================================
// Address: 0x8002ba24 | inflate_reset_output_ptr
// Resets output write pointer to the page buffer base.
// ============================================================================
void inflate_reset_output_ptr(void) {
    g_InflateOutputPtr = g_InflatePageBuffer;
}

// ============================================================================
// Address: 0x8002cca0 | inflate_flush_output
// Flushes the inflate window buffer to the output destination.
// Copies window contents to output and resets window position.
// ============================================================================
void inflate_flush_output(void) {
    if (g_InflateWindowPos != 0) {
        memcpy(g_InflateDestBase + g_InflateDestOffset,
               g_InflateWindowBuf, g_InflateWindowPos);
        g_InflateDestOffset += g_InflateWindowPos;
        g_InflateWindowPos = 0;
    }
}

// ============================================================================
// Address: 0x8002ccf4 | inflate_parse_gzip_header
// Parses and validates a gzip file header (RFC 1952).
// Validates magic (0x1f 0x8b), compression method (8 = deflate),
// skips metadata fields (MTIME, XFL, OS), optional extra/name fields.
// Asserts on unsupported flags (FTEXT, FHCRC).
// ============================================================================
void inflate_parse_gzip_header(void) {
    u8 magic1, magic2, method, flags;

    // Validate magic number
    magic1 = inflate_read_byte();
    magic2 = inflate_read_byte();
    if (magic1 != 0x1f || magic2 != 0x8b) {
        halt_stub_assert();  // Not gzip
    }

    // Validate compression method (must be 8 = deflate)
    method = inflate_read_byte();
    if (method != 8) {
        halt_stub_assert();
    }

    // Read flags
    flags = inflate_read_byte();
    if (flags & 0x20) halt_stub_assert();  // FENCRYPT not supported
    if (flags & 0x02) halt_stub_assert();  // FHCRC not supported

    // Skip MTIME (4), XFL (1), OS (1) = 6 bytes
    inflate_read_byte();
    inflate_read_byte();
    inflate_read_byte();
    inflate_read_byte();
    inflate_read_byte();
    inflate_read_byte();

    // Skip FEXTRA field if present
    if (flags & 0x04) {
        u16 nExtraLen = inflate_read_byte() | (inflate_read_byte() << 8);
        for (u16 i = 0; i < nExtraLen; i++) {
            inflate_read_byte();
        }
    }

    // Skip FNAME if present (null-terminated string)
    if (flags & 0x08) {
        while (inflate_read_byte() != 0) { }
    }

    // FCOMMENT not expected
    if (flags & 0x10) {
        halt_stub_assert();
    }
}

// ============================================================================
// Address: 0x8002cf40 | inflate_decompress
// Top-level decompression entry point. Processes all DEFLATE blocks
// until the final block is reached. Resets bit buffer state first.
// Returns 0 on success.
// ============================================================================
u32 inflate_decompress(void) {
    u32 bFinalBlock;
    u32 result;

    g_InflateWindowPos = 0;
    g_InflateBitCount = 0;
    g_InflateBitBuffer = 0;

    do {
        result = inflate_block_dispatch(&bFinalBlock);
        if (result != 0) {
            halt_stub_assert();
            return result;
        }
    } while (bFinalBlock == 0);

    // Push back unused bytes
    u32 nUnusedBytes = g_InflateBitCount >> 3;
    while (nUnusedBytes > 0) {
        g_InflateBitCount -= 8;
        g_InflateReadPos--;
        nUnusedBytes--;
    }

    inflate_flush_output();
    return 0;
}

// ============================================================================
// Address: 0x8002cb78 | inflate_block_dispatch
// Reads DEFLATE block header (BFINAL + BTYPE) and dispatches to the
// appropriate block decoder.
//   BTYPE 0 = stored (uncompressed)
//   BTYPE 1 = fixed Huffman
//   BTYPE 2 = dynamic Huffman
//   BTYPE 3 = reserved (error)
// Returns 0 on success.
// ============================================================================
u32 inflate_block_dispatch(u32* pFinalFlag) {
    // Read BFINAL (1 bit)
    inflate_ensure_bits(1);
    *pFinalFlag = g_InflateBitBuffer & 1;
    g_InflateBitBuffer >>= 1;
    g_InflateBitCount--;

    // Read BTYPE (2 bits)
    inflate_ensure_bits(2);
    u32 nBlockType = g_InflateBitBuffer & 3;
    g_InflateBitBuffer >>= 2;
    g_InflateBitCount -= 2;

    inflate_reset_output_ptr();

    switch (nBlockType) {
    case 0: return inflate_stored_block();
    case 1: return inflate_fixed_block();
    case 2: return inflate_dynamic_block();
    default:
        halt_stub_assert();
        return 0xFFFFFFFF;
    }
}

// ============================================================================
// Address: 0x8002c2d0 | inflate_stored_block
// Decodes a stored (uncompressed) DEFLATE block.
// Reads LEN/NLEN pair, copies LEN raw bytes to output window.
// ============================================================================
// (Large function - standard stored block per RFC 1951)

// ============================================================================
// Address: 0x8002c464 | inflate_fixed_block
// Decodes a fixed Huffman coded block using predefined code lengths
// per RFC 1951 Section 3.2.6:
//   Lit 0-143:   8 bits
//   Lit 144-255: 9 bits
//   Lit 256-279: 7 bits
//   Lit 280-287: 8 bits
//   Dist 0-29:   5 bits
// Builds tables and calls inflate_decode_codes.
// ============================================================================
// (Large function - builds fixed tables then calls decode loop)

// ============================================================================
// Address: 0x8002c5dc | inflate_dynamic_block
// Decodes a dynamic Huffman coded block. Reads:
//   HLIT (5 bits) = number of literal/length codes - 257
//   HDIST (5 bits) = number of distance codes - 1
//   HCLEN (4 bits) = number of code length codes - 4
// Then reads code length alphabet, decodes literal/distance trees,
// and calls inflate_decode_codes.
// ============================================================================
// (Large function - full dynamic tree construction per RFC 1951)

// ============================================================================
// Address: 0x8002ba30 | inflate_build_huffman_table
// Builds a multi-level Huffman lookup table from code lengths.
// Used by all block types. Handles code length validation, canonical
// ordering, and sub-table generation for codes longer than root bits.
// Returns 0 on success, 1 on oversubscribed tree.
// ============================================================================
// (Very large function - core Huffman table builder)

// ============================================================================
// Address: 0x8002beac | inflate_decode_codes
// Main decode loop for literal/length and distance Huffman codes.
// Reads codes from bit stream, emits literals to window, copies
// match references from sliding window. Flushes when window is full.
// ============================================================================
// (Very large function - core inflate decode loop)
