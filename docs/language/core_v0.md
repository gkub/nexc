# nex Core Language — Version 0

This document is the **normative specification** for the first implementable subset of nex: what the reference compiler (`nexc`) should accept, reject, and mean for programs that use only Core v0 features.

Later phases (arrays, concurrency, realtime regions, rich math types, and so on) extend this core. They are described at a high level in [nex.md](../../nex.md); when they land, they get their own spec sections or versioned addenda.

---

## 1. Goals and scope

Core v0 exists to support:

- a **hand-written lexer and parser** with clear token and AST shapes
- **semantic analysis** (scopes, types, mutability, control flow, returns)
- **predictable integer behavior** (no silent undefined overflow)
- **C-like surface syntax**: semicolons, parentheses on `if`/`while`, familiar declaration forms

Core v0 intentionally does **not** specify arrays, references, generics, macros, headers/modules linking, or concurrency. Those appear in later documents.

---

## 2. Source units and the entry point

### 2.1 Translation units

A **source file** (`.nexs`) is a single **translation unit**. The lexer reads a sequence of Unicode code points; for Core v0, programs are assumed to be **UTF-8** text and the **grammar is defined over ASCII** plus newline. Non-ASCII in comments or string literals (when added) is a separate concern; Core v0 only requires ASCII for identifiers and operators.

### 2.2 Top-level: declarations only

At the **outermost scope** of a translation unit, only **declarations** are allowed—no executable **statements**.

Allowed at top level:

- function definitions (`fn` … `{` … `}`)
- `const` constants (see §6)

Not allowed at top level:

- `let` / `let mut` (these are for function bodies)
- `return`, `if`, `while`, expression statements, assignment statements, or bare expressions

This matches the usual C/C++ model: runnable code lives in functions, not floating at file scope.

### 2.3 Executable programs vs library units

**Executable program:** the implementation expects a single distinguished function:

```text
fn main() -> void { ... }
```

or:

```text
fn main() -> i32 { ... return 0; }
```

The host toolchain uses `main` as the program entry point, analogous to hosted C/C++. Exact linkage and startup (arguments, environment) are defined when the driver and runtime exist; Core v0 only requires that these two signatures are valid and that `main` is the entry for “run this binary.”

**Library unit:** a translation unit may define any number of functions and `const` items **without** defining `main`. Such a unit is suitable for linking into a larger program. A future compiler flag or link rule may select another entry symbol for embedded or freestanding targets.

---

## 3. Lexical structure

### 3.1 Whitespace and semicolons

Whitespace (space, tab, newline, carriage return) is **insignificant** except as required to separate tokens. Statements are **terminated by `;`**. A newline does **not** end a statement.

### 3.2 Comments

- **Line comment:** `//` starts a comment that runs to the end of the line.
- **Block comment:** `/`* … `*/`. Block comments **do not nest**.

### 3.3 Identifiers

An **identifier** is a non-empty sequence of ASCII letters, digits, and underscores, where the first character is not a digit.

```text
[a-zA-Z_][a-zA-Z0-9_]*
```

### 3.4 Keywords (reserved)

Core v0 reserves the following spellings:

`bool`, `const`, `else`, `false`, `fn`, `if`, `let`, `mut`, `return`, `true`, `void`, `while`

### 3.5 Literals

- **Integer literals:** decimal sequences of digits, with optional `0x` / `0X` prefix for hexadecimal (exact literal grammar can be tightened in implementation; at minimum: decimal and hex for Core v0).
- **Boolean literals:** `true` and `false`.
- **String literals:** double-quoted byte strings such as `"Hello, world!"`. Core v0 supports escapes for `\"`, `\\`, `\n`, `\t`, and `\r`.

The type of an unsuffixed integer literal is resolved by **context** (expected type) or by **default** (e.g. `i32`) when ambiguous; the compiler should define a small set of rules and diagnostics for ambiguity. (Initial implementations may default literals to `i32` and require explicit typing otherwise.)

### 3.6 Operators and punctuation

At minimum, Core v0 uses:

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Unary: `-` (negation)
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Assignment: `=` (only in assignment statements, not at top level)
- Grouping and structure: `(`, `)`, `{`, `}`, `,`, `;`, `:`, `->`

---

## 4. Types

### 4.1 Scalar types

Core v0 includes fixed-width integers and Boolean:


| Type                      | Meaning                            |
| ------------------------- | ---------------------------------- |
| `i8`, `i16`, `i32`, `i64` | Signed two’s-complement integers   |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers                  |
| `bool`                    | Boolean                            |
| `str`                     | String literal data                |
| `void`                    | “No value” (function returns only) |


There is no implicit `void` value; `void` is only a **return type**.

`str` is the type of string literals. Core v0 treats `str` as a lightweight
frontend type suitable for built-in printing. Its runtime representation is
defined later when lowering/runtime support exists.

### 4.2 Boolean and C-like conditions

`bool` is a real type with values `true` and `false`.

For `**if` and `while` conditions** (and later, similar contexts), the expression may be:

- of type `bool`, or
- of any **integer** type: **zero** is false, **any nonzero** value is true (C-like truthiness).

This is **not** the same as JavaScript’s notion of “truthy” objects or arrays: Core v0 only has scalars here; reference types come later.

### 4.3 `const` vs `let` (not JavaScript `const`)

In JavaScript, `const` means “this binding cannot be reassigned,” but **object contents** may still mutate. nex avoids that confusion:

- `**let`** introduces a **runtime** binding inside a function: a name for a value in a stack frame (conceptually). It is **immutable** unless `mut` is used.
- `**let mut`** introduces a **mutable** runtime binding: reassignment with `=` is allowed.
- `**const`** at module or block scope introduces a **compile-time constant**: its initializer must be a **constant expression** evaluable at compile time. `const` names a single immutable value for use in types and expressions; it does **not** mean an immutable runtime `let` binding in the JavaScript sense.

For Core v0, `const` appears at **module (top) level** and optionally in inner scopes if the implementation chooses; the initializer must be a constant expression.

---

## 5. Arithmetic and overflow

All integer arithmetic is **defined**. There is **no undefined behavior** for overflow in Core v0.

### 5.1 Unsigned integers

For `u8` … `u64`, arithmetic operations **wrap** modulo 2^n for the bit width n.

### 5.2 Signed integers

For `i8` … `i64`, two’s-complement **wrapping** is the defined behavior for `+`, `-`, `*`, and operations that reduce results to the type’s width in the obvious way.

### 5.3 Constant expressions

If the compiler can **evaluate an expression at compile time** and that evaluation **would overflow** the target type, the compiler must issue a **diagnostic**. The default severity should be **error** (recommended) so invalid literals and `const` initializers never build silently.

### 5.4 Division and remainder

- **Division by zero** is a **compile-time error** when the divisor is provably zero.
- For dynamic divisors, behavior is implementation-defined in Core v0; a quality implementation should trap or report at runtime. (This may be tightened in a later spec revision.)

---

## 6. Items and functions

### 6.1 Function syntax

```text
fn name(param: Type, ...) -> ReturnType {
    statements
}
```

Parameters are immutable bindings unless extended later; Core v0 does not include references, so parameters are passed by value (copy). For small scalars this is the natural model.

`ReturnType` is either a scalar type or `void`. If `void`, `return;` is used with no expression. If not `void`, every path that leaves the function must `return` an expression of that type (the compiler enforces this once control-flow analysis exists).

### 6.2 `const` items

```text
const NAME: Type = constant_expression;
```

`NAME` is visible in the rest of the module according to normal scope rules. The right-hand side must be a **constant expression** (literals, other `const` names, and operators obeying §5).

### 6.3 Built-in printing

Core v0 provides two built-in functions:

```text
print(str) -> void
println(str) -> void
```

These are semantic built-ins recognized by the compiler frontend. Their runtime
implementation is defined when native code generation and runtime support exist.

---

## 7. Statements (inside functions)

Core v0 statements:

- **Block:** `{` statement* `}`
- **Expression statement:** a **call** followed by `;`, e.g. `f(a, b);`, when the callee returns `void`. (Using the result of a non-`void` call as a statement is reserved for a later revision or may be a warning.)
- **Let:** `let x: Type = expr;` or `let mut x: Type = expr;`
- **Assignment:** `place = expr;` where `place` is a `let mut` binding
- **Return:** `return;` (only in `void` functions) or `return expr;`
- **If:** `if (cond) stmt [else stmt]` — `else` binds to the **innermost** `if` (standard dangling-else rule)
- **While:** `while (cond) stmt`

Every `let` must have an **initializer** in Core v0.

---

## 8. Expressions and precedence

Expressions include:

- literals, identifiers
- string literals
- function calls: `f(a, b, ...)`
- unary `-` and `!`
- binary arithmetic and comparisons
- logical `&&` and `||` (**short-circuit**)
- parentheses

**Recommended precedence** (high to low), aligned with common C-family languages:

1. postfix: calls `f()`
2. unary: `-`, `!`
3. multiplicative: `*`, `/`, `%`
4. additive: `+`, `-`
5. relational: `<`, `>`, `<=`, `>=`
6. equality: `==`, `!=`
7. logical AND: `&&`
8. logical OR: `||`

`&&` and `||` short-circuit: the right-hand side is not evaluated if the result is fixed by the left-hand side.

---

## 9. Name resolution and scopes

- Function parameters and `let` bindings introduce names in **block scope**.
- Inner blocks may **shadow** outer names.
- `const` at module scope is visible **below** its definition in the same file unless the implementation adopts forward references for constants (recommended: define before use for simplicity in v0).

---

## 10. Diagnostics and source locations

Implementations should attach **source spans** to every diagnostic: at least a start and end offset into the source buffer (or line/column derived from it). Errors should be **actionable**: expected token, undefined name, type mismatch, missing return, illegal assignment to immutable binding, etc.

---

## 11. Debugging and AST introspection (tooling)

The reference compiler should expose:

- **`--dump-tokens`**: token stream with kinds and spans (for lexer debugging).
- **`--dump-ast`**: human-readable tree of the parsed program (for parser debugging).

A natural extension is **`--dump-ast-dot`** (or similar): emit a [Graphviz](https://graphviz.org/) `dot` graph of the AST for a function or whole unit so it can be rendered as an image. This is a tooling feature, not a language semantic, but it is part of the expected developer experience for nex.

---

## 12. Valid and invalid examples

### 12.1 Minimal program (`void` main)

```c
fn main() -> void {
    return;
}
```

### 12.2 `i32` main with exit code

```c
fn main() -> i32 {
    return 0;
}
```

### 12.3 Library-style unit (no `main`)

```c
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

### 12.4 Immutability

```c
fn f() -> void {
    let x: i32 = 1;
    x = 2; // invalid: x is not mut
}
```

### 12.5 Top-level statement (invalid)

```c
let x: i32 = 1; // error: let only inside functions
```

---

## 13. Relation to later phases

Features sketched in [nex.md](../../nex.md) (arrays, `spawn`, channels, `realtime fn`, matrix types, resource tracking) **extend** Core v0. Until they are specified in this `docs/language/` tree, they are **not** part of the normative Core v0 grammar.

---

## Summary


| Topic      | Core v0 rule                                                                        |
| ---------- | ----------------------------------------------------------------------------------- |
| Top level  | Declarations only (`fn`, `const`)                                                   |
| Entry      | `fn main() -> void` or `fn main() -> i32` for executables                           |
| Statements | `;` terminated; no top-level statements                                             |
| Integers   | Fixed-width signed/unsigned; wrapping arithmetic; compile-time overflow diagnostics |
| Conditions | `bool` or integer (zero / nonzero)                                                  |
| Bindings   | `let`, `let mut`, `const` (compile-time)                                            |
| `const`    | Not JS `const`; compile-time constant only                                          |
| Tooling    | `--dump-tokens`, `--dump-ast`; Graphviz optional                                    |


