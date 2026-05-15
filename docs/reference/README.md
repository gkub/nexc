# nex Reference Documentation

Long-lived, user-facing reference for the nex language and its toolchain. These
pages describe current behavior; tutorial prose, design drafts, and compiler
implementation notes live elsewhere.

## If you are new to nex

1. **Run something:** [`docs/user/tutorial.md`](../user/tutorial.md) and small programs under `examples/`.
2. **Look up exact rules:** this `reference/` tree (language, toolchain, runtime).
3. **Check planned work:** [`docs/IMPLEMENTATION_BACKLOG.md`](../IMPLEMENTATION_BACKLOG.md) is the ordered living to-do list. For a quick **feature inventory** (what exists / gaps), see [language/feature_inventory.md](language/feature_inventory.md).

Milestone history and long-horizon vision: [`nex.md`](../../nex.md).

## Contents

| Section | Role |
| -------- | ---- |
| [language/](language/README.md) | Normative language rules by topic (declarations, types, expressions, statements, functions, built-ins). |
| [toolchain/](toolchain/README.md) | How to invoke `nexc`, build, test, interpret dumps and diagnostics. |
| [runtime/](runtime/README.md) | Intended home for stable runtime/stdlib contracts; today mostly bootstrap notes. |

**Which doc wins?**

- **[`docs/reference/`](language/README.md)** describes **what the compiler does now**. Keep it in sync with tests.
- **[`docs/IMPLEMENTATION_BACKLOG.md`](../IMPLEMENTATION_BACKLOG.md)** describes implementation order, not current behavior.
- **`nexc.sh` vs `build/nexc`:** The shell helper automates configure/build/test; **`build/nexc` is the canonical CLI** surface documented under toolchain.

## Scope Rules

- Put stable user-facing behavior here.
- Keep design brainstorming in `docs/design/`.
- Keep implementation walkthroughs in `NEXC_HOLY_BOOK.md`.

## Versioning Strategy

- Prefer updating **`docs/reference/`** + tests whenever behavior changes.

## Suggested Build Order (for doc authors)

1. Language reference pages stay authoritative per feature area.
2. Toolchain reference stays aligned with `build/nexc`, CMake, and CTest.
3. Runtime reference grows once APIs stabilize beyond `runtime/nex_runtime.c`.
4. Cross-link tutorial ↔ reference ↔ design when topics overlap.
