# Language Functions And Calls

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Function Declaration Form](#function-declaration-form)
- [Parameters](#parameters)
- [Return Types](#return-types)
- [Call Expressions](#call-expressions)
- [Call Statements](#call-statements)
- [Built-in Calls](#built-in-calls)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define current function and call behavior in implemented language semantics.

## Status

- Stability: provisional
- Applies to: Core v0 + current built-in slice

## Function Declaration Form

```text
fn name(param: Type, ...) -> ReturnType {
    statements
}
```

## Parameters

- Parameters currently use explicit type annotations.
- Parameter names are resolved in function-local scope.
- Current semantics treat parameters as immutable bindings.

## Return Types

- Functions return either scalar types or `void`.
- `void` functions use `return;`.
- Non-`void` functions return typed expressions.
- Current semantic analysis enforces return-path requirements for non-`void`
  functions.

## Call Expressions

Call expression shape:

```text
callee(arg1, arg2, ...)
```

Current checks:

- callee must resolve to callable symbol
- argument count must match expected signature
- argument types must match expected parameter types

## Call Statements

Standalone call statements are currently valid when call return type is `void`.

Discarding non-`void` call results is currently rejected.

## Built-in Calls

Current built-ins:

- `print(str) -> void`
- `println(str) -> void`
- `readln() -> str`
- `parse_i32(str) -> i32`
- `parse_u64(str) -> u64`
- `parse_bool(str) -> bool`
- `input_ok() -> bool`

Built-ins are language-defined calls (not user-declared functions), resolved by
frontend semantics and lowered through runtime boundary.

## Examples

```nex
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> void {
    println("sum:");
    println(readln());
    return;
}
```

## Cross References

- `docs/reference/language/declarations_and_modules.md`
- `docs/reference/language/expressions.md`
- `docs/reference/language/builtins_and_io.md`
