# Declarations and Modules

## Table of Contents

- [Summary](#summary)
- [Syntax](#syntax)
- [Translation Unit Model](#translation-unit-model)
- [Top-Level Declarations](#top-level-declarations)
- [Function Declarations](#function-declarations)
- [Module Constants](#module-constants)
- [Entry Point Rules](#entry-point-rules)
- [Current Module Scope Limits](#current-module-scope-limits)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

This page defines the current top-level shape of a `.nexs` translation unit.
Today, top level means function declarations and module constants. Imports,
packages, and user-defined top-level types are not implemented yet.

## Syntax

```text
translation-unit ::= top-level-declaration*

top-level-declaration ::= function-declaration
                        | module-constant

function-declaration ::= "fn" name "(" parameters? ")" "->" type block
module-constant      ::= "const" name ":" type "=" expression ";"
```

## Translation Unit Model

One `.nexs` file is one translation unit in inspection modes.

Native compile mode also accepts multiple `.nexs` paths before `-o`. In that
mode, `nexc` concatenates the files in command-line order and parses the result
as one translation unit. There is no import graph yet.

## Top-Level Declarations

Allowed at top level:

- function declarations/definitions (`fn`)
- module constants (`const`)

Top-level executable statements are rejected (e.g. `let`, `if`, `while`,
assignment, call statements).

## Function Declarations

Function declaration shape:

```text
fn name(param: Type, ...) -> ReturnType {
    statements
}
```

Parameter and call rules are covered in [functions_and_calls.md](functions_and_calls.md).

## Module Constants

Module constant shape:

```text
const NAME: Type = expression;
```

Constant initializers must satisfy current constant-expression rules.

## Entry Point Rules

When compiling executable programs, `main` must satisfy:

- name: `main`
- no parameters
- return type: `void` or `i32`

## Current Module Scope Limits

Not currently implemented:

- imports/modules across multiple files
- package/module namespace system
- user-defined type declarations at top level
- array-typed module constants

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

## See Also

- [functions_and_calls.md](functions_and_calls.md)
- [statements.md](statements.md)
- [compiler_cli.md](../toolchain/compiler_cli.md)
