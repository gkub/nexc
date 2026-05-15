# Definite assignment (locals)

This note describes the **flow-sensitive** check that allows

```text
let mut x: T;
```

(no `= expr`) while rejecting reads of `x` until the compiler can show that **some
assignment ran on every path** that reaches the read.

## What is tracked

Each `let` / `let mut` binding in a function gets a fresh **binding id** at
declaration time. The analyzer keeps a set `definiteAssign` of ids that are
**definitely assigned** at the current program point.

- **Parameters** start in `definiteAssign` (they always have a value).
- Module **`const`** symbols are not locals; they are always readable and skip
  this set.
- Reading a non-const local through a **name** or **array binding** requires
  its binding id to be in `definiteAssign`.
- **`x = expr`** (and indexed assign when implemented for arrays) adds `x`’s id
  after the usual type and mutability checks.

## Join points (`if` / `else`)

After a structured branch, states from incoming paths are merged:

- **Intersection** — a binding is readable only if it was readable on **all**
  paths that can fall through to the merge. Example: `if` without `else` can skip
  the then-body; a local assigned only in the then-branch is **not** readable
  after the whole `if`.
- **Union-like cases** — when one arm definitely returns, only the other arm’s
  fall-through path merges with the pre-`if` state (see `mergeDefiniteAssignAfterIf`
  in `src/frontend/semantic.cpp`).

## Loops (conservative)

Assignments in **`while`** and **`for`** bodies are **not** assumed to run. After
the loop, definite-assignment state is restored to a snapshot from **before** the
loop (or, for `for`, reset to **after the init clause only**). This matches
“maybe zero iterations” and avoids unsoundly treating loop bodies as having run.

## Parsing vs semantics

- **`let x: T;`** without **`mut`** and without **`=`** is a **parse error** (use
  `let mut` for declare-without-init).
- **Fixed-size arrays** without an initializer are rejected for now (until
  per-element or whole-array rules are chosen); see `IMPLEMENTATION_BACKLOG.md`
  Wave C.

## Backends

Typed IR **`DeclareLocal`** omits `value` when there is no initializer; MLIR
lowering allocates the stack `memref` slot and **does not** emit an initial
`memref.store`. Semantic analysis guarantees no `LoadLocal` happens before a store.

## See also

- [`docs/reference/language/statements.md`](../reference/language/statements.md) —
  user-facing syntax and examples.
- `src/frontend/semantic.cpp` — `definiteAssign_`, `requireReadableLocal`, loop
  and `if` handling.
