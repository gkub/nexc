# Language feature inventory

Succinct checklist of **implemented surface** (and a few prominent gaps). Normative detail lives in the linked reference pages; this file is for quick scanning and onboarding.

**Order:** roughly compilation pipeline, then syntax from declarations inward.

---

## Translation unit and pipeline

- **Single merged TU:** multiple `.nexs` paths on the CLI concatenate in order; one namespace. [declarations_and_modules.md](declarations_and_modules.md)
- **Pipeline:** lexer → parser → AST → semantic → typed IR → MLIR → LLVM IR → native executable (when link recipe configured). [compiler_cli.md](../toolchain/compiler_cli.md), [inspection_modes.md](../toolchain/inspection_modes.md)

## Top-level declarations

- **`fn name(params) -> Return { ... }`** — functions; `main` special-casing for entry. [declarations_and_modules.md](declarations_and_modules.md), [functions_and_calls.md](functions_and_calls.md)
- **`const NAME: T = constexpr;`** — module constants; limited forms for initializer IR. [declarations_and_modules.md](declarations_and_modules.md)
- **No** separate modules / `import` / headers yet.

## Types (implemented)

- **Scalars:** `i8` … `u64`, `bool`, `void`. [types.md](types.md)
- **`str`:** string type with runtime support; literals; some builtins. [types.md](types.md)
- **Fixed arrays `[T; N]`:** locals, **`fn` parameters and returns**, module **`const`** (full array literal initializer); indexing and `let mut` element assign. **`main`** may not return an array type. Uninitialized `let mut a: [T; N];` is still rejected. [types.md](types.md)

## Locals and definite assignment

- **`let name: T = expr;`** — immutable; must have initializer.
- **`let mut name: T = expr;`** — mutable with initial value.
- **`let mut name: T;`** — **scalar** mutable storage, no `=`; readable only after **flow-sensitive definite assignment** (`if`/`else` joins, conservative `while`/`for`). [statements.md](statements.md), [definite_assignment.md](../../design/definite_assignment.md)
- **Fixed arrays:** uninitialized `let mut a: [T; N];` **not** accepted yet (semantic reject).

## Assignment

- **`name = expr;`** — `let mut` names only. [statements.md](statements.md)
- **`arr[i] = expr;`** — `let mut` array binding. [statements.md](statements.md), [expressions.md](expressions.md)

## Control flow

- **`if (cond) stmt` / `else`** — `bool` or integer condition-like. [statements.md](statements.md)
- **`while (cond) stmt`**. [statements.md](statements.md)
- **C-style `for (init; cond; step) body`** — any clause may be omitted; `init` may be `let` (scoped to full `for`), assignment, or void call; `step` assignment or void call; typed IR lowers to `While` shape. [statements.md](statements.md)
- **`break;` / `continue;`** — innermost loop; for `for`, `continue` runs `step` before next condition. [statements.md](statements.md)
- **`return;` / `return expr;`** — void vs non-void rules; missing return path diagnostic. [statements.md](statements.md), [functions_and_calls.md](functions_and_calls.md)

## Expressions

- **Literals:** integers (typed), `true`/`false`, string literals. [expressions.md](expressions.md)
- **Arithmetic / compare / bitwise** — as in reference; integer-focused. [expressions.md](expressions.md)
- **`&&` / `||`** — short-circuit; condition-like operands in runtime code; module `const` folding uses distinct lowering detail. [expressions.md](expressions.md)
- **Unary `-`, `!`, `~`** — where supported. [expressions.md](expressions.md)
- **Calls** — user functions and builtins; `void` only as statement. [functions_and_calls.md](functions_and_calls.md)
- **Indexing** — fixed arrays. [expressions.md](expressions.md)

## Built-ins and I/O

- **`print` / `println`** — format string with `{}` placeholders + typed args; legacy forms where still allowed. [builtins_and_io.md](builtins_and_io.md)
- **`readln() -> str`**, **`parse_*`**, **`input_ok()`** — stdin helpers. [builtins_and_io.md](builtins_and_io.md)

## Statements (other)

- **Blocks `{ ... }`**, scoped `let`. [statements.md](statements.md)
- **Expression statement** — call / assign only where grammar allows. [statements.md](statements.md)

## Not implemented (representative)

- Module system / imports, user `struct`, enums, traits, generics.
- Heap allocation, ownership, **pointers**.
- Type inference (`let x = expr` without `: T`).
- **`Option` / `Result`**, richer file I/O.
- Arrays in function signatures and module `const` (see backlog).

---

## See also

- [Language reference index](README.md)
- [Implementation backlog](../../IMPLEMENTATION_BACKLOG.md)
