# Types

## Table of Contents

- [Summary](#summary)
- [Syntax](#syntax)
- [Integer Types](#integer-types)
- [Boolean Type](#boolean-type)
- [String Type](#string-type)
- [Void Type](#void-type)
- [Fixed-Size Arrays](#fixed-size-arrays)
- [Type Rules](#type-rules)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

nex currently has fixed-width integers, `bool`, `str`, `void`, and fixed-size
arrays for local bindings. Types appear in function signatures, local
declarations, module constants, and array declarations where supported.

## Syntax

```text
type ::= integer-type
       | "bool"
       | "str"
       | "void"
       | "[" type ";" integer-literal "]"

integer-type ::= "i8" | "i16" | "i32" | "i64"
               | "u8" | "u16" | "u32" | "u64"
```

Built-in scalar types:

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

## Fixed-Size Arrays

Fixed arrays are implemented for **locals** (`let` / `let mut`): element types are
the usual scalar built-ins (`i8`–`u64`, `bool`). **`void`**, **`str`**, and nested
`[…]` element types are rejected. **`N`** must be a positive decimal or hex integer
literal in the type.

**Meaning:** `[T; N]` is **exactly `N` elements** of type `T`. Storage is inline
(stack slots lowered as ranked `memref`s). This is not a growable vector and not a
slice; see [arrays_vectors_linalg.md](../../design/arrays_vectors_linalg.md).

**Not supported yet:** array-typed **function parameters**, **returns**, and
**module `const`** initializers (diagnosed in semantic analysis).

**Initialization today:** a `let` / `let mut` array binding **must** provide a
**full** array literal of length `N` on the declaration. There is no
“uninitialized `[T; N]` then fill in a loop” form yet; that work is scheduled
after **`for`** loops and **definite assignment** for scalars—see
[`docs/IMPLEMENTATION_BACKLOG.md`](../../IMPLEMENTATION_BACKLOG.md).

```nex
let xs: [i32; 4] = [1, 2, 3, 4];
let mut ys: [i32; 2] = [10, 20];
ys[0] = 5;
return xs[0] + ys[1];
```

## Type Rules

- Arithmetic operators require integer operands.
- Comparison/equality produce `bool`.
- Function call arguments must match declared parameter types.
- `main` currently must return `void` or `i32`.
- Array literals must match a contextual `[T; N]` type (from a `let` binding), or
  every element must agree so the compiler can infer one `[T; N]` type.
- Indexing requires an integer index; constant indices are checked against `N` when
  the index is compile-time known.

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

## See Also

- [IMPLEMENTATION_BACKLOG.md](../../IMPLEMENTATION_BACKLOG.md)
- [arrays_vectors_linalg.md](../../design/arrays_vectors_linalg.md)
- [expressions.md](expressions.md)
- [statements.md](statements.md)
