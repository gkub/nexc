# nex Reference Documentation

Long-lived, user-facing reference for the nex language and its toolchain—contrasted with tutorial prose in `docs/user/`, design drafts in `docs/design/`, and the educational compiler tour in `NEXC_HOLY_BOOK.md`.

## If you are new to nex

1. **Run something:** [`docs/user/core_v0_tutorial.md`](../user/core_v0_tutorial.md) and small programs under `examples/`.
2. **Look up exact rules:** this `reference/` tree (language → types, statements, builtins, …).
3. **See what is intentionally not implemented yet:** [`docs/IMPLEMENTATION_BACKLOG.md`](../IMPLEMENTATION_BACKLOG.md) lists the **ordered next milestones** (e.g. `for` loops before uninitialized locals + definite assignment). [`language/statements.md`](language/statements.md) also summarizes **current gaps** at statement level so you are not surprised when `for` or “declare without `=`” do not parse.

Milestone history and long-horizon vision: [`nex.md`](../../nex.md).

## Contents

| Section | Role |
| -------- | ---- |
| [language/](language/README.md) | Normative language rules by topic (types, statements, builtins, …). |
| [toolchain/](toolchain/README.md) | How to invoke `nexc`, build, test, interpret dumps and diagnostics. |
| [runtime/](runtime/README.md) | Intended home for stable runtime/stdlib contracts; today mostly bootstrap notes. |

**Which doc wins?**

- **[`docs/reference/`](language/README.md)** describes **what the compiler does now** (keep it in sync with tests).
- **[`docs/language/core_v0.md`](../language/core_v0.md)** is a **frozen early snapshot** for history and diffing—not the live checklist (see [`IMPLEMENTATION_BACKLOG.md`](../IMPLEMENTATION_BACKLOG.md)).
- **`nexc.sh` vs `build/nexc`:** The shell helper automates configure/build/test; **`build/nexc` is the canonical CLI** surface documented under toolchain.

## Scope Rules

- Put stable user-facing behavior here.
- Keep design brainstorming in `docs/design/`.
- Keep implementation walkthroughs in `NEXC_HOLY_BOOK.md`.
- Keep frozen snapshots under `docs/language/` where they already exist.

## Versioning Strategy

- Prefer updating **`docs/reference/`** + tests whenever behavior changes.
- Treat `docs/language/core_v0.md` as a historical anchor unless you intentionally revise that snapshot.

## Suggested Build Order (for doc authors)

1. Language reference pages stay authoritative per feature area.
2. Toolchain reference stays aligned with `build/nexc`, CMake, and CTest.
3. Runtime reference grows once APIs stabilize beyond `runtime/nex_runtime.c`.
4. Cross-link tutorial ↔ reference ↔ design when topics overlap.
