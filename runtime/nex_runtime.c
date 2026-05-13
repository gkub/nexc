#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/*
 * Tiny bootstrap runtime for nex.
 *
 * Nex owns the source-level meaning of print/println. This file is only the
 * current binary bridge used by the native driver: generated LLVM IR calls these
 * stable symbols, and the link step (`ld.lld`) pulls this archive into the executable.
 * Later, once Nex can express enough low-level operations itself, this runtime
 * can move toward being implemented in Nex.
 */

static bool nex_runtime_last_ok = true;

/*
 * Write exactly `length` bytes to stdout using Linux write(2).
 *
 * We intentionally loop until all bytes are written (or a hard failure occurs)
 * because write(2) may complete partially. This keeps language-level print
 * semantics deterministic even on unusual file descriptor states.
 */
void nex_runtime_print_str(const char* data, uint64_t length) {
    while (length > 0) {
        const ssize_t wrote = write(1, data, (size_t)length);
        if (wrote <= 0) {
            nex_runtime_last_ok = false;
            return;
        }
        data += (size_t)wrote;
        length -= (uint64_t)wrote;
    }
    nex_runtime_last_ok = true;
}

void nex_runtime_println_str(const char* data, uint64_t length) {
    nex_runtime_print_str(data, length);
    if (!nex_runtime_last_ok) {
        return;
    }
    static const char newline = '\n';
    if (write(1, &newline, 1) != 1) {
        nex_runtime_last_ok = false;
        return;
    }
    nex_runtime_last_ok = true;
}

/*
 * First deliberately tiny input primitive.
 *
 * `readln()` in Nex lowers to these two runtime calls:
 *
 * - nex_runtime_readln_data(): read a line from stdin and return its byte buffer
 * - nex_runtime_readln_len(): return the byte length from that last read
 *
 * This is not the final string ownership model. It uses one process-global
 * scratch buffer so nex can experiment with input before the language has
 * heap allocation, owned strings, slices, or arrays.
 */
static char nex_runtime_readln_buffer[4096];
static uint64_t nex_runtime_readln_length = 0;

const char* nex_runtime_readln_data(void) {
    nex_runtime_readln_length = 0;
    while (nex_runtime_readln_length + 1 < sizeof(nex_runtime_readln_buffer)) {
        char c = '\0';
        const ssize_t got = read(0, &c, 1);
        if (got == 0) {
            /* EOF: report failure when nothing was read this call. */
            nex_runtime_last_ok = nex_runtime_readln_length > 0;
            break;
        }
        if (got < 0) {
            nex_runtime_last_ok = false;
            nex_runtime_readln_length = 0;
            nex_runtime_readln_buffer[0] = '\0';
            return nex_runtime_readln_buffer;
        }

        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        nex_runtime_readln_buffer[nex_runtime_readln_length++] = c;
    }
    nex_runtime_readln_buffer[nex_runtime_readln_length] = '\0';
    if (nex_runtime_readln_length > 0) {
        nex_runtime_last_ok = true;
    }
    return nex_runtime_readln_buffer;
}

uint64_t nex_runtime_readln_len(void) {
    return nex_runtime_readln_length;
}

static bool ascii_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool parse_u64_ascii(const char* data, uint64_t length, uint64_t* out) {
    uint64_t i = 0;
    while (i < length && ascii_is_space(data[i])) {
        ++i;
    }
    if (i == length) {
        return false;
    }

    uint64_t value = 0;
    bool saw_digit = false;
    while (i < length && data[i] >= '0' && data[i] <= '9') {
        saw_digit = true;
        const uint64_t digit = (uint64_t)(data[i] - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
        ++i;
    }
    while (i < length && ascii_is_space(data[i])) {
        ++i;
    }
    if (!saw_digit || i != length) {
        return false;
    }
    *out = value;
    return true;
}

int32_t nex_runtime_parse_i32(const char* data, uint64_t length) {
    bool negative = false;
    uint64_t i = 0;
    while (i < length && ascii_is_space(data[i])) {
        ++i;
    }
    if (i < length && (data[i] == '-' || data[i] == '+')) {
        negative = data[i] == '-';
        ++i;
    }

    uint64_t value = 0;
    bool saw_digit = false;
    while (i < length && data[i] >= '0' && data[i] <= '9') {
        saw_digit = true;
        const uint64_t digit = (uint64_t)(data[i] - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            nex_runtime_last_ok = false;
            return 0;
        }
        value = value * 10ULL + digit;
        ++i;
    }
    while (i < length && ascii_is_space(data[i])) {
        ++i;
    }
    if (!saw_digit || i != length) {
        nex_runtime_last_ok = false;
        return 0;
    }

    if (negative) {
        if (value > 2147483648ULL) {
            nex_runtime_last_ok = false;
            return 0;
        }
        nex_runtime_last_ok = true;
        return (int32_t)(-(int64_t)value);
    }
    if (value > 2147483647ULL) {
        nex_runtime_last_ok = false;
        return 0;
    }
    nex_runtime_last_ok = true;
    return (int32_t)value;
}

uint64_t nex_runtime_parse_u64(const char* data, uint64_t length) {
    uint64_t value = 0;
    nex_runtime_last_ok = parse_u64_ascii(data, length, &value);
    return nex_runtime_last_ok ? value : 0;
}

_Bool nex_runtime_parse_bool(const char* data, uint64_t length) {
    /* Keep bool parsing intentionally explicit: "true", "false", "1", "0". */
    if (length == 4 && data[0] == 't' && data[1] == 'r' && data[2] == 'u' &&
        data[3] == 'e') {
        nex_runtime_last_ok = true;
        return 1;
    }
    if (length == 5 && data[0] == 'f' && data[1] == 'a' && data[2] == 'l' &&
        data[3] == 's' && data[4] == 'e') {
        nex_runtime_last_ok = true;
        return 0;
    }
    if (length == 1 && data[0] == '1') {
        nex_runtime_last_ok = true;
        return 1;
    }
    if (length == 1 && data[0] == '0') {
        nex_runtime_last_ok = true;
        return 0;
    }
    nex_runtime_last_ok = false;
    return 0;
}

_Bool nex_runtime_last_ok_flag(void) {
    return nex_runtime_last_ok ? 1 : 0;
}

static void print_u64_decimal(uint64_t u) {
    if (u == 0) {
        nex_runtime_print_str("0", 1);
        return;
    }
    char buf[32];
    int n = 0;
    uint64_t t = u;
    while (t > 0) {
        buf[n++] = (char)('0' + (t % 10));
        t /= 10;
    }
    char forward[32];
    for (int i = 0; i < n; ++i) {
        forward[i] = buf[n - 1 - i];
    }
    nex_runtime_print_str(forward, (uint64_t)n);
}

void nex_runtime_print_u64(uint64_t v) {
    print_u64_decimal(v);
}

/*
 * Signed decimal printing without relying on printf. Negative magnitudes use
 * unsigned two's-complement arithmetic so `INT64_MIN` is handled portably.
 */
void nex_runtime_print_i64(int64_t v) {
    uint64_t uv = (uint64_t)v;
    if ((int64_t)uv >= 0) {
        print_u64_decimal(uv);
        return;
    }
    nex_runtime_print_str("-", 1);
    print_u64_decimal(~uv + 1);
}

void nex_runtime_print_bool(_Bool v) {
    if (v) {
        nex_runtime_print_str("true", 4);
    } else {
        nex_runtime_print_str("false", 5);
    }
}
