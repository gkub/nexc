# Toolchain Diagnostics Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Diagnostic Shape](#diagnostic-shape)
- [Severity](#severity)
- [Source Location Format](#source-location-format)
- [Caret Snippet Format](#caret-snippet-format)
- [Common Error Categories](#common-error-categories)
- [Exit Code Interaction](#exit-code-interaction)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define user-visible compiler diagnostic behavior and output format.

## Status

- Stability: provisional
- Applies to: current lexer/parser/semantic/compiler-driver diagnostics

## Diagnostic Shape

Diagnostics currently print as text records including:

- file path
- line and column
- severity
- message
- source snippet with caret underline when available

## Severity

Current user-facing severity is primarily:

- `error`

Warnings/notes may be added in later revisions.

## Source Location Format

Canonical format:

```text
path/to/file.nexs:<line>:<column>: error: <message>
```

## Caret Snippet Format

Typical snippet:

```text
  | source line text
  |     ^~~~
```

Caret span may cover one token or wider expression depending on diagnostic
context.

## Common Error Categories

- lexical errors (unterminated literals, unsupported escapes)
- parse errors (unexpected token, missing delimiters)
- semantic errors:
  - undefined names
  - type mismatches
  - invalid assignment mutability
  - invalid `main` signature
  - missing return paths
  - literal out-of-range / constant evaluation errors
  - invalid built-in argument usage

## Exit Code Interaction

- diagnostics errors cause non-zero exit (`1`)
- argument/usage errors return `2`

## Examples

```text
tests/semantic_return_type_mismatch.nexs:2:12: error: cannot return value of type `bool` from function returning `i32`
  |     return true;
  |            ^~~~
```

## Cross References

- `docs/reference/toolchain/compiler_cli.md`
- `docs/reference/toolchain/inspection_modes.md`
- `docs/user/frontend.md`
