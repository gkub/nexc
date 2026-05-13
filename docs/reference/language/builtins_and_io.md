# Built-ins and Input/Output

## Table of Contents

- [Summary](#summary)
- [Built-in Signatures](#built-in-signatures)
- [Printing](#printing)
- [Input](#input)
- [Current Runtime Model](#current-runtime-model)
- [Examples](#examples)
- [Design Notes For Future I/O](#design-notes-for-future-io)
- [See Also](#see-also)

## Summary

nex currently has language-defined built-ins for printing, reading one line from
stdin, parsing simple values, and checking whether the last fallible input or
parse operation succeeded.

## Built-in Signatures

Current built-ins:

```text
print(fmt: str, ...) -> void
println(fmt: str, ...) -> void
readln() -> str
parse_i32(str) -> i32
parse_u64(str) -> u64
parse_bool(str) -> bool
input_ok() -> bool
```

These are language built-ins recognized by semantic analysis and lowered through
the compiler backend/runtime boundary.

## Printing

### `print(fmt: str, ...) -> void`

Writes formatted bytes to stdout.

Formatting applies when the first argument is a string literal:

- Placeholders are exactly `{}` (one pair of braces). Literal `{` / `}` escapes may be added later.
- Each `{}` matches one following argument, in order. Supported argument types: integers (`i*`/`u*`), `bool`, and `str`.
- `print("...{}...", ...)` writes the formatted bytes only.

Legacy convenience when there are **no** placeholders:

- `print(x)` with a single `str` expression (not necessarily a literal), e.g.
  `print(readln())`, writes that string.

### `println(fmt: str, ...) -> void`

Same formatting behavior as `print`, then writes one newline (`\n`) after the
whole expansion.

Legacy convenience when there are **no** placeholders:

- `println(x)` with a single `str` expression writes that string and appends one
  trailing newline.

Escape sequences in string literals follow the usual rules (`\n`, `\t`, `\"`, `\\`, `\r`).

## Input

### `readln() -> str`

- `readln()` reads one line from stdin.
- Returned value excludes trailing line ending characters.
- Current intended use is direct flow to output or calls that immediately consume
  the value.
- `input_ok()` reports success/failure of the last fallible input/parse operation.

### `parse_i32(str) -> i32`

Parses signed decimal text into `i32`. On failure, returns `0` and sets
`input_ok()` to `false`.

### `parse_u64(str) -> u64`

Parses unsigned decimal text into `u64`. On failure, returns `0` and sets
`input_ok()` to `false`.

### `parse_bool(str) -> bool`

Accepts `true`, `false`, `1`, or `0`. On failure, returns `false` and sets
`input_ok()` to `false`.

### `input_ok() -> bool`

Reports success or failure of the last fallible input or parse operation.

## Current Runtime Model

The current runtime behavior is bootstrap-oriented:

- `str` values lower as pointer+length at backend/runtime boundary.
- `readln()` currently uses temporary runtime scratch storage.
- This is intentionally not the final owned string/slice model.

## Examples

```nex
fn main() -> void {
    print("name: ");
    println(readln());
    return;
}
```

```nex
fn main() -> i32 {
    let value: i32 = parse_i32(readln());
    if (input_ok()) {
        return value;
    } else {
        return 0 - 1;
    }
}
```

## Design Notes For Future I/O

- Language-level I/O contract remains Nex-owned.
- Runtime implementation can vary by platform while preserving that contract.
- File/stdin/stdout/stderr/pipes should be defined with explicit resource/failure
  semantics before expanding built-ins.

## See Also

- [runtime/README.md](../runtime/README.md)
- [functions_and_calls.md](functions_and_calls.md)
- [io_v1_spitball.md](../../design/io_v1_spitball.md)
