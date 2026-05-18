# Types

## Table of Contents

- [Summary](#summary)
- [Syntax](#syntax)
- [Integer Types](#integer-types)
- [Floating-Point Types](#floating-point-types)
- [Boolean Type](#boolean-type)
- [String Type](#string-type)
- [Void Type](#void-type)
- [Fixed-Size Arrays](#fixed-size-arrays)
- [Type Rules](#type-rules)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

nex currently has fixed-width integers, IEEE-754 floats, `bool`, `str`, `void`,
and fixed-size arrays. Types appear in function signatures, local declarations,
module constants, and array declarations where supported.

## Syntax

```text
type ::= integer-type
       | float-type
       | "bool"
       | "str"
       | "void"
       | "[" type ";" integer-literal "]"

integer-type ::= "i8" | "i16" | "i32" | "i64"
               | "u8" | "u16" | "u32" | "u64"

float-type ::= "f32" | "f64"
```

Built-in scalar types:

- signed integers: `i8`, `i16`, `i32`, `i64`
- unsigned integers: `u8`, `u16`, `u32`, `u64`
- floats: `f32`, `f64`
- boolean: `bool`
- string: `str`
- no-value return type: `void`

## Integer Types

- Fixed-width integer semantics are defined.
- Unsigned arithmetic wraps modulo `2^n`.
- Signed arithmetic follows two's-complement wrapping behavior.
- Integer literals are type-checked against expected context and diagnosed when
  out of range.

## Floating-Point Types

- `f32` and `f64` are IEEE-754 single- and double-precision scalar types.
- Float literals use decimal syntax such as `1.0`, `0.5`, `6.02e23`; without
  context they default to `f64`.
- Context can choose `f32`, for example `let x: f32 = 1.25;`.
- There are no implicit integer/float coercions: write matching literal forms
  and parameter types explicitly.

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

Fixed arrays are implemented for **locals**, **function parameters**, **function
return types**, and **module `const`** (initializer must be a compile-time
full array literal, same rules as locals). Element types may be the usual scalar
built-ins (`i8`–`u64`, `f32`, `f64`, `bool`) or another fixed array, so nested arrays such as
`[[i32; 2]; 2]` are valid. **`void`** and **`str`** array elements are rejected.
**`N`** must be a positive decimal or hex integer literal in the type.

**Meaning:** `[T; N]` is **exactly `N` elements** of type `T`. Nested array
dimensions are ordered outer-to-inner: `[[i32; 3]; 2]` has shape `2 x 3`.
Locals and array-typed `const` values lower as ranked `memref`s; parameters and
returns use the same ranked `memref<Nx...xT>` ABI at the MLIR boundary. This is
not a growable vector and not a slice; see
[arrays_vectors_linalg.md](../../design/arrays_vectors_linalg.md).

**`main`:** the entry function may still only return `void` or `i32` (not an
array type).

**Uninitialized locals:** `let mut a: [T; N];` without `=` is accepted for fixed
arrays. The compiler tracks definite assignment per element up to 65536 flat
elements. A scalar element read with compile-time-known indices requires that
exact element to be assigned on every path; a subarray read requires every
element in that slice to be assigned. Reads with non-constant indices require
the whole array binding to be definitely assigned. An indexed store with any
non-constant index pessimistically clears the per-element proof for that array.

**Initialization:** a `let` / `let mut` array binding **must** provide a **full**
array literal of length `N` when it has an initializer. Nested arrays require
full nested literals. Module `const` arrays use the same rule.

```nex
let xs: [i32; 4] = [1, 2, 3, 4];
let mut ys: [i32; 2] = [10, 20];
ys[0] = 5;
let mut grid: [[i32; 2]; 2];
grid[0][0] = 1;
grid[0][1] = 2;
grid[1][0] = 3;
grid[1][1] = 4;
return xs[0] + ys[1] + grid[1][1];
```

## Type Rules

- Arithmetic operators require integer or floating-point operands; `%` is
  integer-only.
- Comparison/equality produce `bool`.
- Function call arguments must match declared parameter types.
- `main` currently must return `void` or `i32`.
- Array literals must match a contextual `[T; N]` type, or every element must
  agree so the compiler can infer one `[T; N]` type.
- Nested array literals infer nested fixed-array types when all element shapes
  and element scalar types agree.
- Indexing requires an integer index; constant indices are checked against the
  current dimension when the index is compile-time known.

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
