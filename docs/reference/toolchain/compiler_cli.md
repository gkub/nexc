# Compiler CLI Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Primary Interface](#primary-interface)
- [Command Forms](#command-forms)
- [Modes](#modes)
- [Exit Codes](#exit-codes)
- [Examples](#examples)
- [Environment Variables](#environment-variables)
- [Cross References](#cross-references)

## Purpose

Define the stable user-facing command-line contract for `build/nexc`.

## Status

- Stability: provisional
- Applies to: post-Core v0 current compiler

## Primary Interface

The primary compiler entrypoint is:

```sh
build/nexc
```

`nexc.sh` is a convenience workflow wrapper, not the canonical language/toolchain
interface.

## Command Forms

```text
nexc (--dump-tokens | --dump-ast | --dump-ast-dot | --dump-ir | --dump-mlir | --dump-llvm | --check) <file.nexs>
nexc <file.nexs> -o <output>
```

## Modes

- `--dump-tokens`: lex and print tokens
- `--dump-ast`: parse and print AST
- `--dump-ast-dot`: parse and print AST as Graphviz DOT
- `--dump-ir`: build and print typed IR
- `--dump-mlir`: lower typed IR and print MLIR
- `--dump-llvm`: lower through MLIR LLVM dialect and print LLVM IR
- `--check`: semantic analysis without IR/MLIR/LLVM dumps
- `<input> -o <output>`: compile to native executable via LLVM IR + host toolchain

## Exit Codes

- `0`: success with no diagnostics errors
- `1`: diagnostics error(s) or runtime exception in compiler path
- `2`: command-line usage/argument error

## Examples

```sh
build/nexc --check examples/hello.nexs
build/nexc --dump-mlir examples/pipeline_walkthrough.nexs
build/nexc --dump-llvm examples/return_42.nexs
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

## Environment Variables

- `NEXC_RUNTIME_LIBRARY`: optional override path to runtime archive used by
  native compile mode.

## Cross References

- `docs/reference/toolchain/README.md`
- `docs/reference/runtime/README.md`
- `docs/user/frontend.md`
- `src/tools/nexc/main.cpp`
