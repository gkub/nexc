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

1. [ ] **Array diagnostics polish** — Improve diagnostics for dynamic-index fallbacks and too-large per-element tracking. Keep fixed-array `push`/`pop` diagnostics pointed toward the future vector story (see [arrays_vectors_linalg.md](design/arrays_vectors_linalg.md)).

2. [ ] **`builtins_and_io.md`** — Short cross-link or example for “read in a loop, then use” using `let mut` + definite assignment, if it fits naturally.

3. [ ] **Const array global lowering (optional optimization)** — Consider `memref.global` / LLVM global constant lowering for small immutable module `const` arrays; keep repeated materialization as the simple fallback. See [const_array_lowering.md](design/const_array_lowering.md).

4. [ ] **Type inference** — `let x = expr` without `: T` when `=` is present (grammar + semantic); explicitly deferred until we want the complexity.

5. [ ] **Richer I/O** — Files, errors; see [io_v1_spitball.md](design/io_v1_spitball.md); pairs with `Option`/`Result`-style types later.

6. [ ] **`Option` / `Result`** — After basic I/O patterns stabilize.

7. [ ] **Toolchain `portability.md`** — Flesh out [toolchain/README.md](reference/toolchain/README.md) stub for cross-host stories.

8. [ ] **Pointers** — Large milestone: address-of / references or `*T`, provenance, `null`, arrays/`str` interaction, LLVM lowering, diagnostics. Split into sub-items when someone starts.

9. [ ] **`str` arrays** — Revisit once the string storage/ownership model is less bootstrap-oriented.

---

## Done (remove rows here as they age out; git history is canonical)

Keep this section brief so the file stays a forward-looking list. Older shipped
work: `for` / `break` / `continue`, formatted `print`/`println`, scalar
`let mut x: T;` + definite assignment + IR/MLIR uninit locals, and core fixed
arrays — see [feature_inventory.md](reference/language/feature_inventory.md),
[types.md](reference/language/types.md), and
[definite_assignment.md](design/definite_assignment.md).

| Shipped | Pointer |
| ------- | ------- |
| C-style `for`, loop control | [statements.md](reference/language/statements.md), examples `for_loop.nexs`, `break_continue.nexs` |
| Scalar definite assignment | [definite_assignment.md](design/definite_assignment.md), tests `semantic_use_before_assign*.nexs` |
| Core fixed arrays: locals, `fn` ABI, module `const`, nested arrays, per-element DA | [types.md](reference/language/types.md), examples `array_fixed.nexs`, `array_abi.nexs`, `array_nested.nexs` |
| Language reference quick-reference pass | [quick_reference.md](reference/language/quick_reference.md), [language README](reference/language/README.md) |
| Definite-assignment diagnostic notes | golden diagnostics `semantic_use_before_assign`, `semantic_if_branch_definite_assign` |

---

## Revision history

| Date | Change |
| ---- | ------ |
| 2026-05-10 | Wave A (`for` loops) shipped; backlog wave structure introduced. |
| 2026-05-15 | Rewrote as ordered laundry list; added [feature_inventory.md](reference/language/feature_inventory.md); folded completed waves into inventory + short “done” stub. |
| 2026-05-16 | Shipped fixed-array `fn` ABI + module `const` arrays; backlog item narrowed to uninit locals / nested arrays. |
| 2026-05-16 | Shipped nested arrays through native execution and `let mut` fixed arrays with per-element definite assignment; backlog narrowed to optional/global/diagnostic array follow-up. |
| 2026-05-16 | Completed reference quick-reference pass and added definite-assignment diagnostic notes. |
