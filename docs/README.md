# `docs/` overview

Top-level documentation for **nex** and **nexc**. Normative “what the compiler does now” lives under **`reference/`**; planning and drafts live here in sibling trees.

## Contents

| Path | Role |
|------|------|
| [`IMPLEMENTATION_BACKLOG.md`](IMPLEMENTATION_BACKLOG.md) | Ordered implementation milestones (source of truth for near-term work). |
| [`reference/README.md`](reference/README.md) | Long-lived **reference** spine: language rules, toolchain, runtime notes. Subfolders each have their own README and topic files. |
| [`reference/language/README.md`](reference/language/README.md) | Language reference by topic (types, statements, builtins, …). |
| [`reference/toolchain/README.md`](reference/toolchain/README.md) | CLI, build, test, dumps, diagnostics, artifacts. |
| [`reference/runtime/README.md`](reference/runtime/README.md) | Runtime / stdlib direction and bootstrap notes. |
| [`reference/PAGE_TEMPLATE.md`](reference/PAGE_TEMPLATE.md) | Optional layout for new reference pages. |
| [`user/core_v0_tutorial.md`](user/core_v0_tutorial.md) | Hands-on tutorial for writing small programs. |
| [`user/frontend.md`](user/frontend.md) | User-facing guide to inspection modes and checking. |
| [`design/`](design/) | Design drafts and architecture notes (not the live spec; see `reference/`). |

## Outside this tree

- **Frozen language snapshots** (e.g. `docs/language/core_v0.md`) are optional historical anchors; the live milestone list is [`IMPLEMENTATION_BACKLOG.md`](IMPLEMENTATION_BACKLOG.md).
- **Compiler implementation walkthrough:** repository root [`NEXC_HOLY_BOOK.md`](../NEXC_HOLY_BOOK.md).
- **Project overview:** repository root [`nex.md`](../nex.md).
