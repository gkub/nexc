# Language Built-ins And Input/Output

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Built-ins](#built-ins)
- [For New Users](#for-new-users)
- [Printing](#printing)
- [Input](#input)
- [Current Runtime Model](#current-runtime-model)
- [Examples](#examples)
- [Design Notes For Future I/O](#design-notes-for-future-io)
- [Cross References](#cross-references)

## Purpose

Define current language-level built-ins for observable I/O behavior.

## Status

- Stability: provisional
- Applies to: current language + experimental stdin slice

## Built-ins

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

## For New Users

If you only want "how do I print?" and "how do I read user input?", start here:

- print text: `print("text");` or `println("text");`
- read one line from user input: `readln()`
- parse text into typed values: `parse_i32(...)`, `parse_u64(...)`, `parse_bool(...)`
- check whether the last input/parse operation succeeded: `input_ok()`

Minimal example:

```nex
fn main() -> void {
    print("name: ");
    println(readln());
    return;
}
```

## Printing

Rust-style formatting applies when the **first** argument is a **string literal**:

- Placeholders are exactly `{}` (one pair of braces). Literal `{` / `}` escapes may be added later.
- Each `{}` matches one following argument, in order. Supported argument types: integers (`i*`/`u*`), `bool`, and `str`.
- `print("…{}…", …)` writes the formatted bytes only.
- `println("…{}…", …)` writes the formatted bytes, then **one** newline (`\n`) after the whole expansion.

Legacy convenience when there are **no** placeholders:

- `print(x)` / `println(x)` with a **single** `str` expression (not necessarily a literal), e.g. `println(readln())`, writes that string; `println` still appends one trailing newline.

Escape sequences in string literals follow the usual rules (`\n`, `\t`, `\"`, `\\`, `\r`).

## Input

- `readln()` reads one line from stdin.
- Returned value excludes trailing line ending characters.
- Current intended use is direct flow to output or calls that immediately consume
  the value.
- `input_ok()` reports success/failure of the last fallible input/parse operation.

### Typed Parsing Helpers

- `parse_i32(str)` parses signed decimal text into `i32`.
- `parse_u64(str)` parses unsigned decimal text into `u64`.
- `parse_bool(str)` accepts `true`, `false`, `1`, or `0`.
- On parse failure, helper returns a zero-ish value and sets `input_ok()` to
  `false`.

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

## Cross References

- `docs/reference/runtime/README.md`
- `docs/design/io_v1_spitball.md`
- `docs/language/core_v0.md`
