# Runtime Reference

This section documents runtime and standard-library-facing behavior as it
stabilizes.

## Current State

The runtime is currently a bootstrap bridge (`runtime/nex_runtime.c`) used by the
native compiler path. It is intentionally small and not yet the final language
runtime contract.

## Planned Pages

- `io.md` (stdin/stdout/stderr, files, pipes, blocking and failure semantics)
- `strings.md` (string representation, ownership, lifetime, interoperability)
- `memory.md` (allocation model, lifetime rules, failure behavior)
- `collections.md` (arrays, slices/views, vectors once stabilized)
- `numerics.md` (numeric runtime contracts supporting language semantics)

## Design Direction

- Nex should own the language/runtime API contract.
- Platform-specific runtime backends may differ internally but must preserve one
  language-level behavior.
