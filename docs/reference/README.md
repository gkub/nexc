# nex Reference Documentation

Long-lived, user-facing reference for the nex language and its toolchain—contrasted with tutorial prose in `docs/user/`, design drafts in `docs/design/`, and the educational compiler tour in `NEXC_HOLY_BOOK.md`.

## Contents

| Section | Role |
| -------- | ---- |
| [language/](language/README.md) | Normative language rules by topic (types, statements, builtins, …). |
| [toolchain/](toolchain/README.md) | How to invoke `nexc`, build, test, interpret dumps and diagnostics. |
| [runtime/](runtime/README.md) | Intended home for stable runtime/stdlib contracts; today mostly bootstrap notes. |

**Ambiguous names**

- **“Reference” vs “Core v0”:** Milestone rules still live in [`docs/language/core_v0.md`](../language/core_v0.md). Reference pages describe **what the current compiler does**, including extensions (for example local fixed arrays) that may run ahead of that milestone doc.
- **`nexc.sh` vs `build/nexc`:** The shell helper automates configure/build/test; **`build/nexc` is the canonical CLI** surface documented under toolchain.

## Scope Rules

- Put stable user-facing behavior here.
- Keep design brainstorming in `docs/design/`.
- Keep implementation walkthroughs in `NEXC_HOLY_BOOK.md`.
- Keep milestone snapshots (like `core_v0`) as historical/versioned baselines where they already exist.

## Versioning Strategy

- `docs/language/core_v0.md` remains the canonical milestone baseline for “original Core v0 shape.”
- New reference pages should be additive and forward-looking.
- When behavior changes, update reference docs and tests in the same change.

## Suggested Build Order (for doc authors)

1. Language reference pages stay authoritative per feature area.
2. Toolchain reference stays aligned with `build/nexc`, CMake, and CTest.
3. Runtime reference grows once APIs stabilize beyond `runtime/nex_runtime.c`.
4. Cross-link tutorial ↔ reference ↔ design when topics overlap.
