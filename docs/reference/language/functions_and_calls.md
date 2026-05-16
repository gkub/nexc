# Functions and Calls

## Table of Contents

- [Summary](#summary)
- [Function Declaration Form](#function-declaration-form)
- [Parameters](#parameters)
- [Return Types](#return-types)
- [Call Expressions](#call-expressions)
- [Call Statements](#call-statements)
- [Built-in Calls](#built-in-calls)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

Functions are top-level declarations with typed parameters and a typed return.
Calls can appear as expressions when they return a value, or as statements when
the callee returns `void`.

## Quick Reference

```nex
fn name(param: Type, ...) -> ReturnType {
    statements
}

let value: i32 = callee(arg1, arg2);
callee_returning_void();
```

## Function Declaration Form

```text
fn name(param: Type, ...) -> ReturnType {
    statements
}
```

Top-level placement rules are covered in [declarations_and_modules.md](declarations_and_modules.md).

## Parameters

- Parameters currently use explicit type annotations.
- Parameter names are resolved in function-local scope.
- Current semantics treat parameters as immutable bindings.

## Return Types

- Functions return scalar types, fixed arrays, or `void`.
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

Standalone call statements are valid when the call return type is `void`.
Discarding non-`void` call results is rejected.

## Built-in Calls

Current built-in signatures:

```text
print(fmt: str, ...) -> void
println(fmt: str, ...) -> void
readln() -> str
parse_i32(text: str) -> i32
parse_u64(text: str) -> u64
parse_bool(text: str) -> bool
input_ok() -> bool
```

Built-ins are language-defined calls (not user-declared functions), resolved by
frontend semantics and lowered through runtime boundary.

Formatting and input behavior are specified in [builtins_and_io.md](builtins_and_io.md).

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

## See Also

- [declarations_and_modules.md](declarations_and_modules.md)
- [expressions.md](expressions.md)
- [builtins_and_io.md](builtins_and_io.md)
