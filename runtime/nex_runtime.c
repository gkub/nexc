#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Tiny bootstrap runtime for Core v0.
 *
 * Nex owns the source-level meaning of print/println. This file is only the
 * current binary bridge used by the native driver: generated LLVM IR calls these
 * stable symbols, and clang links this C implementation into the executable.
 * Later, once Nex can express enough low-level operations itself, this runtime
 * can move toward being implemented in Nex.
 */

void nex_runtime_print_str(const char* data, uint64_t length) {
    /*
     * String literals are lowered as pointer + byte length, not as C strings.
     * That means embedded NUL bytes would still be printed correctly, and the
     * runtime does not need to search for a terminator.
     */
    if (length == 0) {
        return;
    }
    (void)fwrite(data, 1, (size_t)length, stdout);
}

void nex_runtime_println_str(const char* data, uint64_t length) {
    nex_runtime_print_str(data, length);
    (void)fputc('\n', stdout);
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
 * scratch buffer so Core v0.1 can experiment with input before the language has
 * heap allocation, owned strings, slices, or arrays.
 */
static char nex_runtime_readln_buffer[4096];
static uint64_t nex_runtime_readln_length = 0;

const char* nex_runtime_readln_data(void) {
    if (fgets(nex_runtime_readln_buffer,
              (int)sizeof(nex_runtime_readln_buffer),
              stdin) == NULL) {
        nex_runtime_readln_buffer[0] = '\0';
        nex_runtime_readln_length = 0;
        return nex_runtime_readln_buffer;
    }

    /*
     * Return the line content without the platform line ending. That makes
     * `println(readln())` echo exactly one newline: println's own newline.
     */
    size_t length = strlen(nex_runtime_readln_buffer);
    while (length > 0 &&
           (nex_runtime_readln_buffer[length - 1] == '\n' ||
            nex_runtime_readln_buffer[length - 1] == '\r')) {
        nex_runtime_readln_buffer[--length] = '\0';
    }

    nex_runtime_readln_length = (uint64_t)length;
    return nex_runtime_readln_buffer;
}

uint64_t nex_runtime_readln_len(void) {
    return nex_runtime_readln_length;
}
