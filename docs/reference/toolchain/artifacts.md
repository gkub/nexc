# Toolchain Artifacts Reference

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Compiler Binary Artifact](#compiler-binary-artifact)
- [Runtime Artifact](#runtime-artifact)
- [Inspection Output Artifacts](#inspection-output-artifacts)
- [Native Compile Artifacts](#native-compile-artifacts)
- [Test Artifacts](#test-artifacts)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Document the major artifacts produced by build/test/compiler workflows.

## Status

- Stability: provisional
- Applies to: current project build layout

## Compiler Binary Artifact

Primary compiler executable:

```text
build/nexc
```

## Runtime Artifact

Current runtime archive (built by CMake):

```text
build/runtime/libnexrt.a
```

Native compile mode links this archive unless overridden via
`NEXC_RUNTIME_LIBRARY`.

## Inspection Output Artifacts

Compiler inspection modes emit to stdout by default:

- token dump
- AST text dump
- AST DOT dump
- typed IR dump
- MLIR dump
- LLVM IR dump

If redirected, these become user-managed output artifacts.

## Native Compile Artifacts

Native compile mode:

```text
build/nexc <input.nexs> -o <output>
```

Produces:

- requested executable `<output>`
- temporary LLVM IR file (internal handoff, cleaned up by driver)

## Test Artifacts

CTest creates transient artifacts in build directory as needed:

- generated verification files for MLIR/LLVM checks
- temporary executables for executable tests
- captured output for golden/verification helpers

Exact filenames may vary by test helper scripts.

## Examples

```sh
build/nexc --dump-llvm examples/return_42.nexs > build/return_42.ll
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

## Cross References

- `docs/reference/toolchain/compiler_cli.md`
- `docs/reference/toolchain/build_and_test.md`
- `docs/reference/toolchain/inspection_modes.md`
