# Implementation backlog (living to-do list)

Ordered **checklist** for near-term compiler and language work. Prefer **removing or checking off** items when they ship; keep the list short and actionable. If sequencing changes, edit order here first, then update `docs/reference/` and tests.

**Related**

| Document | Role |
| -------- | ---- |
| [Language feature inventory](reference/language/feature_inventory.md) | Succinct inventory of what exists today (and obvious gaps). |
| [Language reference](reference/README.md) | Normative “what the compiler does now.” |
| [nex.md](../nex.md) | Long-horizon vision and evolution buckets. |
| [NEXC_HOLY_BOOK.md](../NEXC_HOLY_BOOK.md) | Educational pipeline tour. |

**Quality bar when an item ships:** reference pages + tests (semantic, goldens, executable if user-visible) + implementation comments; Holy Book only when it helps teaching.

---

## Ordered backlog

Work **top to bottom** unless a dependency forces a swap (note it inline next to the item).

1. [ ] **Fixed-size arrays — next milestone scope** — Decide and implement: (a) arrays on **parameters / returns / module `const`**, (b) **uninitialized** `let mut a: [T; N];` and/or **per-element** definite assignment, (c) or stay with **must initialize** full literal only. Reconcile with [arrays_vectors_linalg.md](design/arrays_vectors_linalg.md), then update [types.md](reference/language/types.md), [statements.md](reference/language/statements.md), [feature_inventory.md](reference/language/feature_inventory.md).

2. [ ] **Array story — documentation pass** — Once (1) is decided, update [arrays_vectors_linalg.md](design/arrays_vectors_linalg.md) if rules differ from the draft; ensure examples and goldens match.

3. [ ] **Definite assignment — nicer diagnostics (optional)** — Path hints (e.g. which branch skipped assignment); low priority while messages stay correct.

4. [ ] **`builtins_and_io.md`** — Short cross-link or example for “read in a loop, then use” using `let mut` + definite assignment, if it fits naturally.

5. [ ] **Type inference** — `let x = expr` without `: T` when `=` is present (grammar + semantic); explicitly deferred until we want the complexity.

6. [ ] **Richer I/O** — Files, errors; see [io_v1_spitball.md](design/io_v1_spitball.md); pairs with `Option`/`Result`-style types later.

7. [ ] **`Option` / `Result`** — After basic I/O patterns stabilize.

8. [ ] **Toolchain `portability.md`** — Flesh out [toolchain/README.md](reference/toolchain/README.md) stub for cross-host stories.

9. [ ] **Pointers** — Large milestone: address-of / references or `*T`, provenance, `null`, arrays/`str` interaction, LLVM lowering, diagnostics. Split into sub-items when someone starts.

---

## Done (remove rows here as they age out; git history is canonical)

Keep this section ** brief** so the file stays a forward-looking list. Older shipped work: `for` / `break` / `continue`, formatted `print`/`println`, **scalar** `let mut x: T;` + definite assignment + IR/MLIR uninit locals — see [feature_inventory.md](reference/language/feature_inventory.md), [definite_assignment.md](design/definite_assignment.md).

| Shipped | Pointer |
| ------- | ------- |
| C-style `for`, loop control | [statements.md](reference/language/statements.md), examples `for_loop.nexs`, `break_continue.nexs` |
| Scalar definite assignment | [definite_assignment.md](design/definite_assignment.md), tests `semantic_use_before_assign*.nexs` |

---

## Revision history

| Date | Change |
| ---- | ------ |
| 2026-05-10 | Wave A (`for` loops) shipped; backlog wave structure introduced. |
| 2026-05-15 | Rewrote as ordered laundry list; added [feature_inventory.md](reference/language/feature_inventory.md); folded completed waves into inventory + short “done” stub. |
