# nex Reference Documentation

This directory is the long-lived, user-facing reference for the nex language and
its toolchain.

The goal is to grow this into the primary documentation surface in the style of
established language references (C/C++/Python-style structure): precise,
navigable, and stable as the language evolves.

## Structure

- `language/`: normative language reference by feature area
- `toolchain/`: compiler CLI, build/test flow, diagnostics, output formats
- `runtime/`: runtime and standard-library-facing APIs as they become stable

## Scope Rules

- Put stable user-facing behavior here.
- Keep design brainstorming in `docs/design/`.
- Keep implementation walkthroughs in `NEXC_HOLY_BOOK.md`.
- Keep milestone snapshots (like `core_v0`) as historical/versioned baselines.

## Versioning Strategy

- `language/core_v0.md` remains the canonical milestone baseline.
- New references should be additive and forward-looking.
- When behavior changes, update reference docs and tests in the same change.

## Build Order (Suggested)

1. Language reference skeleton pages
2. Toolchain reference pages (`build/nexc`, flags, modes, diagnostics)
3. Runtime/I/O reference pages once API contracts stabilize
4. Cross-links between tutorial, reference, and design notes
