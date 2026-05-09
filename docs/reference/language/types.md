# Language Types Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Builtin Scalar Types](#builtin-scalar-types)
- [Integer Types](#integer-types)
- [Boolean Type](#boolean-type)
- [String Type](#string-type)
- [Void Type](#void-type)
- [Type Rules (Current)](#type-rules-current)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define the currently implemented type system surface for nex.

## Status

- Stability: provisional
- Applies to: Core v0 + post-v0 experimental stdin slice

## Builtin Scalar Types

Current built-in types:

- signed integers: `i8`, `i16`, `i32`, `i64`
- unsigned integers: `u8`, `u16`, `u32`, `u64`
- boolean: `bool`
- string: `str`
- no-value return type: `void`

## Integer Types

- Fixed-width integer semantics are defined.
- Unsigned arithmetic wraps modulo `2^n`.
- Signed arithmetic follows two's-complement wrapping behavior.
- Integer literals are type-checked against expected context and diagnosed when
  out of range.

## Boolean Type

- `bool` values are `true` and `false`.
- Conditions accept `bool` directly.
- Conditions also accept integers (`0` false, non-zero true) in current language
  semantics.

## String Type

- `str` is the type of string literals.
- Current backend/runtime boundary lowers strings as pointer+length.
- `str` currently powers `print`, `println`, and `readln()` built-ins.

## Void Type

- `void` is valid as a function return type.
- `void` is not a first-class value expression type.

## Type Rules (Current)

- Arithmetic operators require integer operands.
- Comparison/equality produce `bool`.
- Function call arguments must match declared parameter types.
- `main` currently must return `void` or `i32`.

## Examples

```nex
fn main() -> i32 {
    let a: i32 = 40;
    let b: u32 = 2;
    let ok: bool = a > 0;
    if (ok) {
        return a + 2;
    } else {
        return 0;
    }
}
```

## Cross References

- `docs/reference/language/expressions.md`
- `docs/reference/language/statements.md`
- `docs/language/core_v0.md`
