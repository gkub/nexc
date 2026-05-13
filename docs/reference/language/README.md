# Language Reference

Normative behavior of the implemented language surface, split by topic. These
pages describe what the compiler accepts today.

## Contents

| Document | What it defines | Notes |
| -------- | ---------------- | ----- |
| [declarations_and_modules.md](declarations_and_modules.md) | One `.nexs` translation unit, top-level `fn` / `const`, `main`, single namespace. | "Modules" here means file/top-level structure, not `.nexh` imports. |
| [types.md](types.md) | Built-in scalars, fixed arrays `[T; N]` for locals, typing rules. | Arrays are not accepted on function parameters, returns, or module `const` yet. |
| [expressions.md](expressions.md) | Literals, operators, calls, array literals, indexing `a[i]`, precedence. | Logical `&&` and logical `\|\|` are eager, not short-circuiting. |
| [statements.md](statements.md) | Blocks, `let` / `let mut`, assignment, `return`, `if` / `else`, `while`, `for`, `break`, `continue`, call statements. | `for` lowers to `While` in typed IR dumps. |
| [functions_and_calls.md](functions_and_calls.md) | Function signatures, calls as expressions vs statements, built-ins vs user functions. | Top-level placement rules live in declarations. |
| [builtins_and_io.md](builtins_and_io.md) | `print` / `println`, stdin helpers, parse built-ins. | Formatting uses `{}` placeholders from a string-literal first argument. |

## Near-term roadmap (not reference normative text)

Planned language work and implementation order are centralized here. Update that
file when sequencing changes, then update reference pages when behavior ships:

- [`docs/IMPLEMENTATION_BACKLOG.md`](../../IMPLEMENTATION_BACKLOG.md)

## Not Yet Separate Reference Pages

These topics are called out for future split-out pages or live only in design/milestone docs today:

| Planned / elsewhere | Where detail lives for now |
| ------------------- | --------------------------- |
| Lexical structure (tokens, comments) | Compiler + golden tests; Holy Book § lexer |
| Diagnostics wording catalog | [`toolchain/diagnostics.md`](../toolchain/diagnostics.md) |
| Memory, ownership, allocation | `docs/design/` |
| Concurrency | `nex.md`, design notes |
| Full numerics / linear algebra | `docs/design/arrays_vectors_linalg.md` |
