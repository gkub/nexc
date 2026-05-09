# Language Statements Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Statement Forms](#statement-forms)
- [Block Statements](#block-statements)
- [Variable Declarations](#variable-declarations)
- [Assignment Statements](#assignment-statements)
- [Return Statements](#return-statements)
- [If / Else Statements](#if--else-statements)
- [While Statements](#while-statements)
- [Call Statements](#call-statements)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define function-body statement forms currently accepted and implemented.

## Status

- Stability: provisional
- Applies to: Core v0 language surface

## Statement Forms

Current statement forms:

- block: `{ ... }`
- local declaration: `let` / `let mut`
- assignment: `name = expr;`
- return: `return;` or `return expr;`
- conditional: `if (...) stmt [else stmt]`
- loop: `while (...) stmt`
- call statement: `callee(...);` when return type is `void`

## Block Statements

- Blocks create nested lexical scopes.
- Inner scopes can shadow outer names.

## Variable Declarations

- `let x: T = expr;` creates immutable local binding.
- `let mut x: T = expr;` creates mutable local binding.
- Current language requires initializer on `let`.

## Assignment Statements

- Assignment targets currently resolve to mutable local bindings.
- Assigning to immutable `let` is a diagnostic error.

## Return Statements

- `return;` valid only in `void`-returning functions.
- `return expr;` must match function return type.
- Non-`void` functions require all control paths to return.

## If / Else Statements

- `if` condition accepts `bool` or integer condition expressions.
- `else` binds to nearest unmatched `if`.

## While Statements

- `while` condition accepts `bool` or integer conditions.
- Current return analysis does not assume loop execution.

## Call Statements

- Standalone call statements are valid when call result is `void`.
- Discarding non-`void` call results is rejected in current semantics.

## Examples

```nex
fn main() -> i32 {
    let mut x: i32 = 0;
    while (x < 10) {
        if (x % 2 == 0) {
            x = x + 3;
        } else {
            x = x + 1;
        }
    }
    return x;
}
```

## Cross References

- `docs/reference/language/types.md`
- `docs/reference/language/expressions.md`
- `docs/language/core_v0.md`
