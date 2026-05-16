# Language Reference

Normative behavior of the implemented language surface, split by topic. Start
with the quick reference when you want syntax and snippets; use topic pages for
exact rules and edge cases.

## Contents

| Document | What it defines | Notes |
| -------- | ---------------- | ----- |
| [quick_reference.md](quick_reference.md) | Common syntax shapes, built-in prototypes, and small examples. | Best first stop when writing or checking code. |
| [declarations_and_modules.md](declarations_and_modules.md) | One `.nexs` translation unit, top-level `fn` / `const`, `main`, single namespace. | "Modules" here means file/top-level structure, not `.nexh` imports. |
| [types.md](types.md) | Built-in scalars, fixed arrays `[T; N]` including nested arrays, typing rules. | Arrays are accepted for locals, function parameters/returns, and module `const`; `main` may not return an array. |
| [expressions.md](expressions.md) | Literals, operators, calls, array literals, indexing `a[i]` / `a[i][j]`, precedence. | Logical `&&` / `\|\|` short-circuit; module `const` lowers `&&`/`\|\|` eagerly as truthified `Binary` for a simpler initializer IR. |
| [statements.md](statements.md) | Blocks, `let` / `let mut`, assignment, `return`, `if` / `else`, `while`, `for`, `break`, `continue`, call statements. | `for` lowers to `While` in typed IR dumps. |
| [functions_and_calls.md](functions_and_calls.md) | Function signatures, calls as expressions vs statements, built-ins vs user functions. | Top-level placement rules live in declarations. |
| [builtins_and_io.md](builtins_and_io.md) | `print` / `println`, stdin helpers, parse built-ins. | Formatting uses `{}` placeholders from a string-literal first argument. |

## How To Use These Pages

- Use **quick reference** for copy-paste syntax and built-in signatures.
- Use **topic pages** for normative rules and diagnostics-adjacent behavior.
- Use **feature inventory** for a compact “implemented vs gap” scan.
- Use **implementation backlog** for work order. Reference pages should not carry
  competing roadmap lists.

## Feature inventory (non-normative)

Quick scan of implemented surface and prominent gaps: [feature_inventory.md](feature_inventory.md).

## Planning Source

Planned work and implementation order are centralized in
[`docs/IMPLEMENTATION_BACKLOG.md`](../../IMPLEMENTATION_BACKLOG.md). Update that
file when sequencing changes, then update reference pages when behavior ships.

## Not Yet Separate Reference Pages

These topics are called out for future split-out pages or live only in design/milestone docs today:

| Planned / elsewhere | Where detail lives for now |
| ------------------- | --------------------------- |
| Lexical structure (tokens, comments) | Compiler + golden tests; Holy Book § lexer |
| Diagnostics wording catalog | [`toolchain/diagnostics.md`](../toolchain/diagnostics.md) |
| Memory, ownership, allocation | `docs/design/` |
| Concurrency | `nex.md`, design notes |
| Full numerics / linear algebra | `docs/design/arrays_vectors_linalg.md` |
