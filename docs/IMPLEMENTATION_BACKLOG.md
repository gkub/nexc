# Implementation backlog (ordered)

This file is the **single maintained checklist** for **near-term compiler and language work**—the sequence we intend to follow next, with enough context that contributors (and learners reading the repo) know *why* each step exists and *what* to update when it lands.

It complements:

| Document | Role |
| -------- | ---- |
| [`nex.md`](../nex.md) | Long-horizon vision, language evolution buckets, compiler architecture narrative. |
| [`docs/reference/`](reference/README.md) | **Normative reference for what the compiler does today**—must stay in sync with implementation and tests. |
| [`NEXC_HOLY_BOOK.md`](../NEXC_HOLY_BOOK.md) | Educational tour of the compiler pipeline; good home for deep dives *after* a feature exists. |

**How to use this backlog**

1. Work **top to bottom** unless a dependency forces a swap (call that out in a short note here).
2. When a slice **ships**: tick the checklist here, update **reference** (`docs/reference/language/*.md`, toolchain if needed), add or extend **tests** (semantic, IR/MLIR/LLVM goldens, executables), and add a **Holy Book** section if the teaching value is high.
3. When **sequencing** changes: edit this file first so `nex.md` / `README` pointers stay accurate.

---

## Recently landed (for context)

- **Rust-style `print` / `println`:** format string literal with `` `{}` `` placeholders, typed arguments, `println` appends one `\n` after the formatted output; legacy `println(readln())` still allowed. See [`docs/reference/language/builtins_and_io.md`](reference/language/builtins_and_io.md).
- **C-style `for`:** `for (init; condition; step) body` — any clause may be omitted (`for (;;)` uses a constant-true condition in IR); `init` may be `let`, assignment, or void call; `step` is assignment or void call (no `;` before `)`). Typed IR **desugars** to the existing `While` shape so MLIR stays unchanged. `let` in `init` is scoped to the whole `for` (not visible after the loop). Tests: `examples/for_loop.nexs`, goldens + `run_executable_for_loop`.
- **Loop control:** `break;` exits the innermost loop; `continue;` advances to the next iteration. For `for`, `continue` runs the step clause before the next condition check. Tests: `examples/break_continue.nexs`, semantic diagnostics, and `run_executable_break_continue`.

---

## Wave A — `for` loops — **shipped**

`for`, `break`, and `continue` are implemented. Richer `for` initialization forms can be revisited if real examples need them.

---

## Wave B — Uninitialized locals + **definite assignment**

**Goal:** Allow `let mut x: T;` (and optionally `let x: T;` with rules for `mut`) so storage can be filled after declaration—e.g. user input loops—while **rejecting reads** on paths where the compiler cannot prove assignment happened first.

**Before coding**

- [ ] Which types may use uninitialized form in **v1** (recommend starting with **scalars + fixed arrays**; **`str`** may stay “must initialize” until owned/uninit `str` semantics exist).
- [ ] Exact diagnostic style: path-sensitive messages (e.g. “may be uninitialized here: not assigned in `else` branch at line …”).
- [ ] Interaction with `for` / `while` / `if` (loops are the main consumer).

**Implementation note**

- This is a **flow-sensitive** analysis pass (assigned-on-all-paths to a use). Plan data structures (e.g. per-block state, merge at join points) in a short `docs/design/` note if the Holy Book would get too long.

**Learner-facing output**

- [ ] [`docs/reference/language/statements.md`](reference/language/statements.md) — `let` forms, definite assignment rules, examples.
- [ ] Cross-link from [`builtins_and_io.md`](reference/language/builtins_and_io.md) if we show “read in a loop then use” patterns.

---

## Wave C — Tighten **array** story (revisit after A + B)

**Goal:** Make fixed-size arrays feel complete and honest for systems learners: clear rules for initialization, filling by index, and passing initialized data to functions.

We previously outlined **three coherent directions**; **reopen after Wave B**
(uninitialized locals + definite assignment), because those rules change how arrays
are taught and tested.

| Track | Idea | When it shines |
| ----- | ---- | ---------------- |
| **C1 — Explicit initialization** | Require visible initial values (literals, `= expr`, or an explicit `default` spelling later). | Simplest semantics; zero surprises. |
| **C2 — Uninit + definite assignment per element** | Array binding may start uninitialized if every element is **proven** assigned before any read/use. | Pairs with Wave B; matches “fill from input in a loop.” |
| **C3 — Typed “partially initialized” / optional views** | Stronger types for “not yet valid whole array” (heavier). | Longer-term; depends on type system appetite. |

**After Wave A + B,** reopen this section and:

- [ ] Decide which of C1/C2/C3 (or combination) is in scope for the **next** milestone.
- [ ] Align [`docs/reference/language/types.md`](reference/language/types.md) and [`statements.md`](reference/language/statements.md) with reality (today: array locals need a full literal initializer—see examples).
- [ ] Update [`docs/design/arrays_vectors_linalg.md`](design/arrays_vectors_linalg.md) if the language rule changes relative to that design draft.

---

## Explicitly **not** in this wave (carry forward)

| Topic | Note |
| ----- | ---- |
| **Type inference** (`let x = expr` without `: T`) | Deferred by choice; grammar can later allow optional type when `=` is present. Not a prerequisite for Waves A–C. |
| **Richer I/O** (files, structured errors) | See `docs/design/io_v1_spitball.md`; pairs well with `Option`/`Result`-style types later. |
| **`Option` / `Result`** | Composes with I/O and error handling; revisit after input patterns stabilize. |
| **`portability.md` (toolchain)** | Stubbed in [`docs/reference/toolchain/README.md`](reference/toolchain/README.md); still planned, lower urgency than language surface. |
| **Pointers** | Address-of, references or raw `*T`, pointer arithmetic, provenance/aliasing rules, `null` story, interaction with arrays/`str`, LLVM (`i8*` / inbounds GEP), and diagnostics. Large semantic + backend milestone—track here until split into sub-tasks. |

---

## Documentation quality bar (each wave)

For every shipped slice:

1. **Reference** — accurate for a newcomer using only `docs/reference/` + examples.
2. **Tests** — semantic errors, IR/MLIR/LLVM dumps where applicable, at least one **executable** test if behavior is user-visible at runtime.
3. **Comments in implementation** — match the existing educational style (why this pass exists, invariants, non-obvious tradeoffs); prefer a short design note over a 200-line comment in code.
4. **Holy Book** — add or extend a section when the feature is a good teaching moment (e.g. how `for` lowers, how definite assignment is checked).

---

## Revision history (optional)

| Date | Change |
| ---- | ------ |
| 2026-05-10 | Wave A (`for` loops) shipped; pointers added to carry-forward; `nex.md` phased roadmap condensed to evolution buckets. |

Maintainers: append a row when reordering waves or completing a wave’s doc checklist.
