# Language Reference

Normative behavior of the **implemented** language surface, split by topic (not by compiler milestone). For the historical Core v0 baseline text, see [`docs/language/core_v0.md`](../../language/core_v0.md).

## Contents

| Document | What it defines | Notes |
| -------- | ---------------- | ----- |
| [types.md](types.md) | Builtin scalars, **fixed arrays `[T; N]`** for locals, typing rules. | Arrays are **not** accepted on function parameters, returns, or module `const` yet. |
| [expressions.md](expressions.md) | Literals, operators, calls, array literals, indexing `a[i]`, precedence. | Logical `&&` `\|\|` are **eager** (not short-circuit) in the current lowering. |
| [statements.md](statements.md) | Blocks, `let` / `let mut`, assignment (including `arr[i] =`), control flow, call statements. | Assignment to `arr[i]` requires **`let mut`** on the array binding. |
| [functions_and_calls.md](functions_and_calls.md) | `fn` syntax, calls as expressions vs statements, builtins vs user functions. | Distinct from **module** scope rules in declarations doc. |
| [builtins_and_io.md](builtins_and_io.md) | `print` / `println`, stdin helpers, parse builtins. | **`print` / `println` take `str` only** — there is no generic “print integer” yet; see doc. |
| [declarations_and_modules.md](declarations_and_modules.md) | One `.nexs` translation unit, top-level `fn` / `const`, `main`, single namespace. | **“Modules” here = file/top-level structure**, not `.nexh` imports (not implemented). |

## Not Yet Separate Reference Pages

These topics are called out for future split-out pages or live only in design/milestone docs today:

| Planned / elsewhere | Where detail lives for now |
| ------------------- | --------------------------- |
| Lexical structure (tokens, comments) | Compiler + golden tests; Holy Book § lexer |
| Diagnostics wording catalog | [`toolchain/diagnostics.md`](../toolchain/diagnostics.md) |
| Memory, ownership, allocation | `docs/design/` |
| Concurrency | `nex.md`, design notes |
| Full numerics / linear algebra | `docs/design/arrays_vectors_linalg.md` |
