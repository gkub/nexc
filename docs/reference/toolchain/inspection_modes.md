# Toolchain Inspection Modes

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Mode Overview](#mode-overview)
- [Token Mode](#token-mode)
- [AST Modes](#ast-modes)
- [Semantic Check Mode](#semantic-check-mode)
- [IR / MLIR / LLVM Modes](#ir--mlir--llvm-modes)
- [Native Compile Mode](#native-compile-mode)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Document the compiler's stage-inspection commands and what each mode guarantees.

## Status

- Stability: provisional
- Applies to: current `build/nexc` interface

## Mode Overview

`build/nexc` supports:

- `--dump-tokens`
- `--dump-ast`
- `--dump-ast-dot`
- `--check`
- `--dump-ir`
- `--dump-mlir`
- `--dump-llvm`
- native compile mode: `<input> -o <output>`

## Token Mode

- Command: `build/nexc --dump-tokens <file.nexs>`
- Stops after lexing and prints token stream with spans.

## AST Modes

- `--dump-ast` prints text AST.
- `--dump-ast-dot` prints Graphviz DOT AST.
- Both run lexer+parser first.

## Semantic Check Mode

- Command: `build/nexc --check <file.nexs>`
- Runs lexer+parser+semantic analysis.
- Emits diagnostics without IR/MLIR/LLVM output.

## IR / MLIR / LLVM Modes

- `--dump-ir`: typed IR after successful semantic analysis.
- `--dump-mlir`: MLIR after typed IR lowering.
- `--dump-llvm`: LLVM IR after MLIR lowering pipeline.
- These modes only proceed when diagnostics contain no errors.

## Native Compile Mode

- Command: `build/nexc <file.nexs> -o <output>`
- Produces native executable via LLVM IR handoff to host toolchain.
- Links runtime archive for current built-ins behavior.

## Examples

```sh
build/nexc --dump-tokens examples/minimal.nexs
build/nexc --dump-ast examples/add.nexs
build/nexc --check examples/hello.nexs
build/nexc --dump-ir examples/pipeline_walkthrough.nexs
build/nexc --dump-mlir examples/pipeline_walkthrough.nexs
build/nexc --dump-llvm examples/return_42.nexs
build/nexc examples/hello.nexs -o build/hello
```

## Cross References

- `docs/reference/toolchain/compiler_cli.md`
- `docs/reference/toolchain/diagnostics.md`
- `docs/user/frontend.md`
