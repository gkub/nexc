# `docs/` overview

Top-level documentation for **nex** and **nexc**. Current user-facing behavior
lives under **`reference/`**; planning and drafts live in sibling trees.

## Start here

| Path | Role |
|------|------|
| [`reference/README.md`](reference/README.md) | Long-lived **reference** spine: language rules, toolchain, runtime notes. Subfolders each have their own README and topic files. |
| [`reference/language/README.md`](reference/language/README.md) | Language reference by topic (types, statements, built-ins, and more). |
| [`reference/toolchain/README.md`](reference/toolchain/README.md) | CLI, build, test, dumps, diagnostics, artifacts. |
| [`user/tutorial.md`](user/tutorial.md) | Hands-on tutorial for writing small programs. |

## Compiler and contributor docs

| Path | Role |
|------|------|
| [`IMPLEMENTATION_BACKLOG.md`](IMPLEMENTATION_BACKLOG.md) | Ordered living to-do list for near-term implementation work. |
| [`reference/language/feature_inventory.md`](reference/language/feature_inventory.md) | Succinct inventory of implemented language surface and prominent gaps. |
| [`../NEXC_HOLY_BOOK.md`](../NEXC_HOLY_BOOK.md) | Educational walkthrough of the compiler pipeline and implementation. |
| [`user/frontend.md`](user/frontend.md) | User-facing guide to inspection modes and semantic checking. |
| [`../nex.md`](../nex.md) | Longer project overview and language-planning context. |

## Drafts and scaffolding

| Path | Role |
|------|------|
| [`design/`](design/) | Design drafts and architecture notes. These are not the live spec; see `reference/` for current behavior. |
| [`reference/runtime/README.md`](reference/runtime/README.md) | Runtime / stdlib direction and bootstrap notes. |
| [`reference/PAGE_TEMPLATE.md`](reference/PAGE_TEMPLATE.md) | Optional layout for new reference pages. |
