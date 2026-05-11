# Language Expressions Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Expression Forms](#expression-forms)
- [Operator Precedence](#operator-precedence)
- [Arrays And Indexing](#arrays-and-indexing)
- [Type Rules (Current)](#type-rules-current)
- [Logical Operators Note](#logical-operators-note)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define expression-level syntax and behavior for the currently implemented language
surface.

## Status

- Stability: provisional
- Applies to: current implemented language surface

## Expression Forms

Current expression forms:

- integer literals
- boolean literals
- string literals
- name references
- function calls
- unary operators: `-`, `!`
- binary operators: `+`, `-`, `*`, `/`, `%`, comparisons, equality, `&&`, `||`
- parenthesized expressions
- array literals: `[expr, expr, …]` (length must match the contextual `[T; N]` type,
  or types must agree for inference)
- postfix indexing: `expr[expr]` on an array value

## Operator Precedence

From highest to lowest:

1. postfix: calls `f(args)`, then indexing `a[i]` (left-associative; e.g. `f()[0]`)
2. unary (`-`, `!`)
3. multiplicative (`*`, `/`, `%`)
4. additive (`+`, `-`)
5. relational (`<`, `>`, `<=`, `>=`)
6. equality (`==`, `!=`)
7. logical and (`&&`)
8. logical or (`||`)

## Arrays And Indexing

- An array **literal** lists element expressions separated by commas inside `[` `]`.
- An array **load** is postfix: the base expression must have a fixed array type;
  the index must be an integer type (`i32` is typical).
- Assignment to `name[index]` requires `let mut` on the array binding.

## Type Rules (Current)

- arithmetic operators currently require integer operands
- comparison/equality produce `bool`
- `!` accepts `bool` or integer-like condition values in current semantics
- call argument count/types are checked against resolved callee signature

## Logical Operators Note

Current `&&` and `||` semantics are eager in the implemented compiler path:

- both operands are evaluated
- then boolean operation is applied

This is intentionally documented to match implementation behavior. It is not
short-circuiting at this stage.

## Examples

```nex
fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 2;
    if ((x / y) > 3 && (x % y) == 0) {
        return 1;
    } else {
        return 0;
    }
}
```

## Cross References

- `docs/language/core_v0.md`
- `docs/reference/language/README.md`
- `docs/reference/language/builtins_and_io.md`
