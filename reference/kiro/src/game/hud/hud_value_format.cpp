// Address: 0x80049b9c-0x80049f1c | Name: hud_value_format | Date: 2026-08-10
// Numeric value formatting and parsing for HUD items.
// Supports integer display and MM:SS time format with configurable digit count.
//
// Compile-hardening note (2026-09-04): this file previously contained two
// copies of the utility/format functions (an early documentation draft with
// placeholder `if (/* ... */)` conditions and undeclared "register pair"
// locals, followed by a clean rewrite). The defective first copy was removed;
// the clean definitions are kept, and hud_parse_string_to_value (which only
// existed in the draft) was repaired by turning its register-pair reads into
// explicit parameters.

#include "hud_internal.h"

// Address: 0x80049b9c | Name: hud_util_power_of_10
// Returns 10^n. Simple loop: multiply by 10 for each step.
// Used by value formatter for digit extraction.
int hud_util_power_of_10(int n) {
    int result = 1;
    while (n > 0) {
        result *= 10;
        n--;
    }
    return result;
}

// Address: 0x80049bbc | Name: hud_util_digit_count
// Counts digits in a number. Increments until 10^n > value.
// If flag 0x20 set (time mode), returns fixed count for mm:ss format.
// Used by value formatter to determine output width.
int hud_util_digit_count(int value, u32 flags) {
    int count = 1;

    if ((flags & 0x20) == 0) {
        // Normal mode: count digits in value
        while (hud_util_power_of_10(count) <= value) {
            count++;
        }
    }
    // Time mode: fixed count handled by caller

    hud_style_finalize_pair();  // FUN_8038273c
    return count;
}

// Address: 0x80049c38 | Name: hud_format_value_to_string
// MASSIVE: Formats an integer value into a display string.
// Two modes:
//   1) Normal integer (flag 0x20 clear): extracts digits via division by powers of 10,
//      suppresses leading zeros.
//   2) Time format (flag 0x20 set): splits into minutes:seconds (divmod by 60),
//      separator is ':' or 'm ' (flag 0x40).
// Optional suffix string appended. param_7: 0=format value, 1=format max (all 9s).
void hud_format_value_to_string(char* out_buf, int buf_size, int value,
                                int display_value, u32 flags, int suffix_string_id,
                                int format_max) {
    char* write_ptr;
    int digit_count;
    int char_count = 0;
    int work_value = 0;
    (void)display_value;

    if (format_max == 0) {
        work_value = value;
    }

    if ((flags & 0x20) == 0) {
        // Normal integer formatting
        digit_count = hud_util_digit_count(work_value, flags);
        int leading_zero = 0;  // false
        write_ptr = out_buf;

        for (int i = 0; i < digit_count && char_count < buf_size - 1; i++) {
            if (format_max == 0) {
                int power = hud_util_power_of_10(digit_count - i - 1);
                int digit = (work_value / power);
                work_value -= digit * power;

                if (digit != 0 || leading_zero || power == 1) {
                    leading_zero = 1;  // true
                    *write_ptr = (char)digit + '0';
                    char_count++;
                    write_ptr++;
                }
            } else {
                // Max mode: all 9s
                *write_ptr = '9';
                char_count++;
                write_ptr++;
            }
        }
    } else {
        // Time format (mm:ss)
        digit_count = hud_util_digit_count(work_value, flags);
        int seconds = 0, minutes = 0;

        if (format_max == 0) {
            seconds = value % 60;
            minutes = (value - seconds) / 60;
        }

        write_ptr = out_buf;

        // Minutes digits
        for (int i = 0; i < digit_count - 2 && char_count < buf_size - 1; i++) {
            if (format_max == 0) {
                int power = hud_util_power_of_10(digit_count - 3 - i);
                int digit = (minutes / power);
                minutes -= digit * power;
                *write_ptr = (char)digit + '0';
            } else {
                *write_ptr = '9';
            }
            write_ptr++;
            char_count++;
        }

        // Separator
        if (char_count < buf_size - 1) {
            if ((flags & 0x40) == 0) {
                out_buf[char_count] = ':';
            } else {
                out_buf[char_count] = 'm';
                char_count++;
                out_buf[char_count] = ' ';
            }
            char_count++;
        }

        // Seconds digits
        write_ptr = out_buf + char_count;
        for (int i = digit_count - 2; i < digit_count && char_count < buf_size - 1; i++) {
            if (format_max == 0) {
                int power = hud_util_power_of_10(digit_count - i - 1);
                int digit = (seconds / power);
                seconds -= digit * power;
                *write_ptr = (char)digit + '0';
            } else {
                *write_ptr = '9';
            }
            write_ptr++;
            char_count++;
        }
    }

    // Append suffix if provided
    if (suffix_string_id == 0) {
        out_buf[char_count] = '\0';
    } else {
        if (char_count < buf_size - 1) {
            out_buf[char_count] = ' ';
            char_count++;
        }
        string_copy_truncated(out_buf + char_count, suffix_string_id, buf_size - char_count - 1);
        out_buf[buf_size - 1] = '\0';
    }

    hud_style_finalize_full_alt();  // FUN_8038270c
}

// Address: 0x80049ef0 | Name: hud_format_value_simple
// Wrapper for hud_format_value_to_string with no suffix. Convenience for simple numeric display.
void hud_format_value_simple(char* out_buf, int buf_size, int value,
                             int suffix_id, u32 flags) {
    hud_format_value_to_string(out_buf, buf_size, value, value, flags, suffix_id, 0);
}

// Address: 0x80049f1c | Name: hud_parse_string_to_value
// Parses a formatted numeric string back into an integer value.
// Walks the string backwards, identifies digit characters ('0'-'9'), and
// accumulates value using positional powers of 10. In time mode (flag 0x20),
// minutes digits (positions >= 2) are offset by -2 to skip the separator.
//
// Note: the original decompilation read `str` and `flags` from incoming
// registers; they are expressed here as explicit parameters.
int hud_parse_string_to_value(char* str, unsigned int flags) {
    int len;
    int position;
    int result;
    char* ptr;

    // Count string length
    len = 0;
    for (ptr = str; *ptr != '\0'; ptr++) {
        len++;
    }

    len--;
    ptr = str + len;
    position = 0;
    result = 0;

    for (; len >= 0; len--) {
        if (*ptr >= '0' && *ptr <= '9') {
            int digit = *ptr - '0';
            int weight;

            if ((flags & 0x20) == 0) {
                // Normal mode: standard positional
                weight = hud_util_power_of_10(position);
            } else if (position < 2) {
                // Time mode: seconds digits (positions 0-1)
                weight = hud_util_power_of_10(position);
            } else {
                // Time mode: minutes digits (positions 2+, offset by -2)
                weight = hud_util_power_of_10(position - 2);
            }

            result += digit * weight;
            position++;
        }
        ptr--;
    }

    return result;
}
