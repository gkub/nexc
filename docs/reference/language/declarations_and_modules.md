# Language Declarations And Modules

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Translation Unit Model](#translation-unit-model)
- [Top-Level Declarations](#top-level-declarations)
- [Function Declarations](#function-declarations)
- [Module Constants](#module-constants)
- [Entry Point Rules](#entry-point-rules)
- [Current Module Scope Limits](#current-module-scope-limits)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define source-file and top-level declaration structure for currently implemented
nex language behavior.

## Status

- Stability: provisional
- Applies to: Core v0 language surface

## Translation Unit Model

One `.nexs` file is one translation unit in the current compiler.

## Top-Level Declarations

Currently allowed at top level:

- function declarations/definitions (`fn`)
- module constants (`const`)

Top-level executable statements are rejected (e.g. `let`, `if`, `while`,
assignment, call statements).

## Function Declarations

Current function declaration shape:

```text
fn name(param: Type, ...) -> ReturnType {
    statements
}
```

## Module Constants

Current module constant shape:

```text
const NAME: Type = expression;
```

Constant initializers must satisfy current constant-expression rules.

## Entry Point Rules

When compiling executable programs, current `main` constraints are:

- name: `main`
- no parameters
- return type: `void` or `i32`

## Current Module Scope Limits

Not currently covered as stable language behavior:

- imports/modules across multiple files
- package/module namespace system
- user-defined type declarations at top level

## Examples

```nex
const LIMIT: i32 = 10;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    return add(LIMIT, 32);
}
```

## Cross References

- `docs/reference/language/functions_and_calls.md`
- `docs/reference/language/statements.md`
- `docs/language/core_v0.md`
