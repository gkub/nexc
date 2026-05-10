# Toolchain Build And Test

## Table of Contents

- [Purpose](#purpose)
- [Status](#status)
- [Primary Build Flow](#primary-build-flow)
- [Primary Test Flow](#primary-test-flow)
- [Helper Script Role](#helper-script-role)
- [Environment Variables](#environment-variables)
- [CI Expectations](#ci-expectations)
- [Examples](#examples)
- [Cross References](#cross-references)

## Purpose

Define the canonical build/test workflows for compiler contributors and advanced
users.

## Status

- Stability: provisional
- Applies to: current CMake/CTest project layout

## Primary Build Flow

Canonical direct build commands:

```sh
cmake -S . -B build
cmake --build build
```

Primary compiler executable:

```sh
build/nexc
```

## Primary Test Flow

Canonical direct test command:

```sh
ctest --test-dir build --output-on-failure
```

This runs the registered CTest suite (frontend, IR, MLIR/LLVM validations when
available, native executable checks when host toolchain is present).

## Helper Script Role

`nexc.sh` is a convenience wrapper around CMake/CTest workflows:

```sh
./nexc.sh configure
./nexc.sh build
./nexc.sh test
./nexc.sh check
```

It is useful for daily iteration, but the direct interface remains
`build/nexc` + CMake/CTest commands.

## Environment Variables

Common workflow variables:

- `BUILD_DIR` (custom build directory for script workflows)
- `BUILD_TYPE` (e.g. `Release`)
- `CMAKE_GENERATOR` (e.g. `Ninja`)

For runtime archive override during native compile mode:

- `NEXC_RUNTIME_LIBRARY`

## CI Expectations

CI should run:

1. configure
2. build
3. full CTest suite

When MLIR/LLVM tools (including `llc` and `ld.lld`) are available in the CI image, MLIR and LLVM validation
tests plus native executable tests should run as part of normal CI coverage.

## Examples

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

build/nexc --check examples/hello.nexs
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

## Cross References

- `docs/reference/toolchain/compiler_cli.md`
- `docs/reference/toolchain/inspection_modes.md`
- `docs/reference/toolchain/diagnostics.md`
- `README.md`
