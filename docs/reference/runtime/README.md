# Runtime Reference

This subtree will hold **stable** runtime and standard-library-facing contracts (signatures, error modes, ABI) once they stop changing every bootstrap tweak.

## Current State

There is **no per-topic runtime reference page yet.** Today the shipped bridge is C code in [`runtime/nex_runtime.c`](../../../runtime/nex_runtime.c) (printing, minimal stdin, parse helpers). Built-ins are also summarized under [`language/builtins_and_io.md`](../language/builtins_and_io.md).

**Ambiguity:** “Runtime” here means **the small archive linked into native executables** (`libnexrt.a`), not the LLVM project “runtime” or a future Nex standard library namespace.

## Planned Pages

| Planned doc | Would cover |
| ----------- | ----------- |
| `io.md` | stdin/stdout/stderr, files, pipes; blocking and failure semantics. |
| `strings.md` | `str` representation, ownership, lifetime, FFI. |
| `memory.md` | Allocation model and errors once specified. |
| `collections.md` | Slices, growable vectors after language support exists. |
| `numerics.md` | Numeric conventions shared with the language reference. |

## Design Direction

- Nex should own the **language/runtime API** contract.
- Platform backends may differ internally but must preserve one language-level behavior.
