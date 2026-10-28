// Address: 0x8002dea0-0x8002e264 | Name: text_parser | Date: 2026-08-05
// Simple text tokenizer for config/data file parsing.
// Handles: whitespace skipping, # and // comments, quoted strings,
// numeric literals (with 'e' exponent notation), identifiers [a-zA-Z0-9/].
//
// Parser state struct (offset from base):
//   +0x00: int pReadPos (current read pointer)
//   +0x04: int pEndPos (end of buffer)
//   +0x08: char[128] tokenBuf (secondary token copy)
//   +0x88: char[128] token (primary token output)
//   +0x108: int tokenLen (current token length)

#include "common.h"
#include "text_parser.h"

#define TOKEN_MAX_LEN 127

// ============================================================================
// Address: 0x8002dea0 | parser_read_char
// Reads next character from buffer and advances read position.
// Returns current char without advancing past end.
// ============================================================================
char parser_read_char(int* pParser) {
    char* pCur = (char*)*pParser;
    char c = *pCur;
    if (pCur < (char*)pParser[1]) {
        *pParser = (int)(pCur + 1);
    }
    return c;
}

// ============================================================================
// Address: 0x8002dec4 | parser_append_token_char
// Appends a character to the token buffer. Max 127 chars.
// Returns 1 on success, 0 if buffer full.
// ============================================================================
int parser_append_token_char(int pParser, char c) {
    int nLen = *(int*)(pParser + 0x108);
    if (nLen < TOKEN_MAX_LEN) {
        *(char*)(pParser + nLen + 0x88) = c;
        *(int*)(pParser + 0x108) = nLen + 1;
        return 1;
    }
    return 0;
}

// ============================================================================
// Address: 0x8002def4 | parser_read_token
// Reads the next token from the text buffer. Handles:
//   - Whitespace/tab/CR skipping
//   - Single-line comments (# and //)
//   - Quoted strings ("...")
//   - Numeric literals (digits, -, ., e notation with +/-)
//   - Identifiers (a-z, A-Z, 0-9, /)
// Result stored in parser token buffer at offset +0x88.
// ============================================================================
void parser_read_token(int* pParser) {
    char c;
    BOOL bIsNumber;

    c = parser_read_char(pParser);
    pParser[0x42] = 0;  // Reset token length

    // Skip whitespace
    while (!parser_is_eof(pParser) && (c == ' ' || (c - 9) < 2 || c == '\r')) {
        c = parser_read_char(pParser);
    }

    // Skip comments (# or //)
    if (c == '#' || (c == '/' && stream_get_entry_type(pParser) == '/')) {
        do {
            c = parser_read_char(pParser);
            if (c == '\n') break;
        } while (!parser_is_eof(pParser));
        c = parser_read_char(pParser);
    }

    if (parser_is_eof(pParser)) {
        parser_append_token_char((int)pParser, '\0');
        return;
    }

    if (c == '\"') {
        // Quoted string: read until closing quote
        while ((c = parser_read_char(pParser)) != '\"') {
            parser_append_token_char((int)pParser, c);
        }
    } else {
        parser_append_token_char((int)pParser, c);

        if ((c >= '0' && c <= '9') || c == '-' || c == '.') {
            // Numeric literal
            bIsNumber = FALSE;
            while (!parser_is_eof(pParser)) {
                char peek = stream_get_entry_type(pParser);
                if (!((peek >= '0' && peek <= '9') || peek == 'e' ||
                      peek == '.' || (bIsNumber && (peek == '+' || peek == '-')))) {
                    break;
                }
                bIsNumber = (peek == 'e');
                c = parser_read_char(pParser);
                parser_append_token_char((int)pParser, c);
            }
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            // Identifier
            while (!parser_is_eof(pParser)) {
                char peek = stream_get_entry_type(pParser);
                if (!((peek >= '0' && peek <= '9') ||
                      (peek >= 'a' && peek <= 'z') ||
                      (peek >= 'A' && peek <= 'Z'))) {
                    break;
                }
                c = parser_read_char(pParser);
                parser_append_token_char((int)pParser, c);
            }
        }
    }
    parser_append_token_char((int)pParser, '\0');
}

// ============================================================================
// Address: 0x8002e1e4 | parser_read_token_copy
// Reads next token and copies it to the secondary buffer at offset +0x08.
// Returns pointer to the secondary buffer.
// ============================================================================
int* parser_read_token_copy(int* pParser) {
    parser_read_token(pParser);
    strcpy((char*)(pParser + 2), (char*)(pParser + 0x22));
    return pParser + 2;
}

// ============================================================================
// Address: 0x8002e220 | parser_is_eof
// Returns TRUE if the read position has reached or passed the end.
// ============================================================================
BOOL parser_is_eof(int* pParser) {
    return (pParser[1] - *pParser) <= 0;
}

// ============================================================================
// Address: 0x8002e238 | parser_token_equals
// Compares the current token (at pParser + 8) against a string.
// Returns TRUE (1) on match, FALSE (0) on mismatch.
// ============================================================================
BOOL parser_token_equals(int pParser, const char* pStr) {
    return strcmp((char*)(pParser + 8), pStr) == 0;
}

// ============================================================================
// Address: 0x8002e264 | parser_init
// Initializes a text parser state with buffer begin and size.
// ============================================================================
void parser_init(int* pParser, int pBuffer, int nSize) {
    *pParser = pBuffer;       // Read position
    pParser[1] = pBuffer + nSize;  // End position
}
