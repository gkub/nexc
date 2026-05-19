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

1. [ ] **User-defined types design** — Structs/enums first. Decide syntax, layout, constructors, field access, type namespaces, and how enums eventually support `Option` / `Result`.

2. [ ] **Full pointer model** — Extend the MVP: pointer parameters/returns, read-only vs writable pointers, `null` or no `null`, pointer-to-array/`str` interaction, pointer arithmetic/casts, provenance, diagnostics.

3. [ ] **Hash maps / dictionaries design** — Design before implementing. Decide allocation, ownership, key equality, hashing, generics or monomorphization strategy, iteration order, failure behavior, and whether the first surface is closer to C++ `unordered_map` or Python `dict`.

4. [ ] **Richer I/O** — Files, errors; see [io_v1_spitball.md](design/io_v1_spitball.md); pairs with `Option`/`Result`-style types later.

5. [ ] **`Option` / `Result`** — After enum/user-type patterns stabilize.

6. [ ] **Toolchain `portability.md`** — Flesh out [toolchain/README.md](reference/toolchain/README.md) stub for cross-host stories.

7. [ ] **`str` arrays** — Revisit once the string storage/ownership model is less bootstrap-oriented.

8. [ ] **`builtins_and_io.md`** — Short cross-link or example for “read in a loop, then use” using `let mut` + definite assignment, if it fits naturally.

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
| Array definite-assignment diagnostics polish | golden diagnostics `semantic_array_dynamic_index_da`, `semantic_array_da_too_large` |
| IEEE-754 floating-point scalars + numeric formatting | [types.md](reference/language/types.md), [expressions.md](reference/language/expressions.md), [builtins_and_io.md](reference/language/builtins_and_io.md), examples `float_scalar.nexs`, `float_format_print.nexs`, `int_format_print.nexs` |
| Integer bitwise and shifts | [expressions.md](reference/language/expressions.md), example `bitwise_shift.nexs` |
| Const array global lowering | [const_array_lowering.md](design/const_array_lowering.md), example `const_array_global.nexs` |
| Local type inference | [statements.md](reference/language/statements.md), [types.md](reference/language/types.md), example `type_inference.nexs` |
| Pointer MVP | [pointers_mvp.md](design/pointers_mvp.md), example `pointer_scalar.nexs` |

---

## Revision history

| Date | Change |
| ---- | ------ |
| 2026-05-10 | Wave A (`for` loops) shipped; backlog wave structure introduced. |
| 2026-05-15 | Rewrote as ordered laundry list; added [feature_inventory.md](reference/language/feature_inventory.md); folded completed waves into inventory + short “done” stub. |
| 2026-05-16 | Shipped fixed-array `fn` ABI + module `const` arrays; backlog item narrowed to uninit locals / nested arrays. |
| 2026-05-16 | Shipped nested arrays through native execution and `let mut` fixed arrays with per-element definite assignment; backlog narrowed to optional/global/diagnostic array follow-up. |
| 2026-05-16 | Completed reference quick-reference pass and added definite-assignment diagnostic notes. |
| 2026-05-18 | Reprioritized next language work around IEEE-754 floats, integer bitwise/shifts, and a later hash-map design item. |
| 2026-05-18 | Completed array definite-assignment diagnostics polish for dynamic indices and tracking limits. |
| 2026-05-18 | Shipped `f32` / `f64` literals, semantics, IR, MLIR/LLVM lowering, constants, ABI, docs, and executable/golden coverage. |
| 2026-05-18 | Added float formatting for `print` / `println` with `{:.N}` / `{:.Nf}` fixed precision. |
| 2026-05-18 | Shipped integer bitwise and shifts with semantic diagnostics, MLIR/LLVM lowering, and executable/golden coverage. |
| 2026-05-18 | Added integer base formatting for hex and binary output via `{:x}`, `{:X}`, and `{:b}`. |
| 2026-05-18 | Shipped private immutable `memref.global` lowering for dense literal module `const` arrays with materialization fallback. |
| 2026-05-18 | Shipped local-only type inference for initialized `let` / `let mut` declarations. |
| 2026-05-18 | Shipped pointer MVP: `*T`, `&local`, dereference load/store for mutable scalar locals. |
