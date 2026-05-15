# Statements

## Table of Contents

- [Summary](#summary)
- [Syntax Summary](#syntax-summary)
- [Block Statements](#block-statements)
- [Variable Declarations](#variable-declarations)
- [Assignment Statements](#assignment-statements)
- [Return Statements](#return-statements)
- [If / Else Statements](#if--else-statements)
- [While Statements](#while-statements)
- [For Statements](#for-statements)
- [Break and Continue](#break-and-continue)
- [Call Statements](#call-statements)
- [Examples](#examples)
- [See Also](#see-also)

## Summary

Statements appear inside function bodies. They create scopes, declare locals,
assign values, return from functions, branch, loop, control loops, or call
`void` functions.

## Syntax Summary

Current statement forms include:

- block: `{ ... }`
- local declaration: `let` / `let mut`
- assignment: `name = expr;` or `name[index] = expr;` (array element update)
- return: `return;` or `return expr;`
- conditional: `if (...) stmt [else stmt]`
- loops: `while (...) stmt`, **`for (init; condition; step) stmt`** (C-style; see below)
- loop control: `break;`, `continue;` (only inside a `while` or `for` body)
- call statement: `callee(...);` when return type is `void`

## Block Statements

- Blocks create nested lexical scopes.
- Inner scopes can shadow outer names.

## Variable Declarations

```text
let name: Type = expr;
let mut name: Type = expr;
let mut name: Type;
```

- `let x: T = expr;` creates immutable local binding (initializer required).
- `let mut x: T = expr;` creates mutable local binding with an initial value.
- `let mut x: T;` declares mutable storage **without** an initializer. The name
  may be read only after the compiler proves an assignment on **every path** to
  that read (flow-sensitive **definite assignment**). Details and join/loop rules:
  [definite_assignment.md](../../design/definite_assignment.md).
- `let x: T;` (no `mut`, no `=`) is invalid: the parser requires either `= expr`
  or `let mut` for uninitialized declarations.

## Assignment Statements

```text
name = expr;
name[index] = expr;
```

- Assignment targets are either a **mutable local name** or an **indexed mutable
  array** (`arr[i] = …`) where `arr` was declared with `let mut`.
- Assigning to immutable `let` or through an immutable array binding is a diagnostic
  error.

## Return Statements

```text
return;
return expr;
```

- `return;` valid only in `void`-returning functions.
- `return expr;` must match function return type.
- Non-`void` functions require all control paths to return.

## If / Else Statements

```text
if (condition) stmt
if (condition) stmt else stmt
```

- `if` condition accepts `bool` or integer condition expressions.
- `else` binds to nearest unmatched `if`.

## While Statements

```text
while (condition) stmt
```

- `while` condition accepts `bool` or integer conditions.
- Current return analysis does not assume loop execution.

## For Statements

C-style header with three clauses separated by `;`:

```text
for ( init ; condition ; step ) body
```

- **`init`:** optional. May be empty (`;` as the first token after `(`). Otherwise
  **`let` / `let mut`** (with required `= expr` and trailing `;`), an **assignment**
  statement, or a **void call** statement—same forms as ordinary statements, each
  ending with `;` except the `let` form already includes its `;`.
- **`condition`:** optional. If omitted (`for (init;; step)` or `for (;;)`), the
  loop is **infinite** at the language level: lowering uses a constant-`true`
  condition in typed IR. If present, it is an expression followed by `;`; type
  rules match **`while`** (`bool` or integer condition-like).
- **`step`:** optional. If present, it is an **assignment** or **void call**
  **without** a trailing semicolon; the closing **`)`** of the `for` header ends the
  clause. If omitted, write `)` immediately after the second `;` (e.g. `for (let mut i: i32 = 0; i < 10;)`).
- **`body`:** a single statement (often a `{ ... }` block).

**Scope:** a `let` in `init` is visible in **`condition`**, **`body`**, and
**`step`**, and **not** after the `for` statement (one inner lexical scope for the
whole construct).

**Lowering:** the typed IR builder desugars `for` to **`init` + `while`** so the
existing `While` → `scf.while` path is reused; dumps therefore show `While`, not a
distinct `For` operation. The `for` **update** clause is a separate child block on
that `While` so `continue` can run it before the next condition check.

## Break and Continue

```text
break;
continue;
```

- **`break;`** exits the innermost enclosing `while` or `for`.
- **`continue;`** advances the innermost enclosing `while` or `for` to its next
  iteration. For `for`, the **update** clause runs before the condition is
  evaluated again.
- Either form outside any loop is a semantic error.
- **`continue`** is not allowed in the `for` **update** clause (the third header
  expression).

## Call Statements

```text
callee(...);
```

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

Counted iteration with **`for`** (see also [`examples/for_loop.nexs`](../../../examples/for_loop.nexs)):

```nex
fn main() -> i32 {
    let mut sum: i32 = 0;
    for (let mut i: i32 = 0; i < 10; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}
```

## See Also

- [types.md](types.md)
- [expressions.md](expressions.md)
- [functions_and_calls.md](functions_and_calls.md)
- [definite_assignment.md](../../design/definite_assignment.md) (compiler design)
- [IMPLEMENTATION_BACKLOG.md](../../IMPLEMENTATION_BACKLOG.md)
