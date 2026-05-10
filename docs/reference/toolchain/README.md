# Toolchain Reference

How developers and CI **invoke** the compiler, reproduce builds, read dumps, and interpret errors. Implementation internals belong in `NEXC_HOLY_BOOK.md`; language meaning belongs under [`language/`](../language/README.md).

## Contents

| Document | What it defines | Notes |
| -------- | ---------------- | ----- |
| [compiler_cli.md](compiler_cli.md) | Forms of `nexc` (inspect modes vs `… -o out`). | **`nexc.sh` is not the canonical interface** — `build/nexc` is. Multi-input compile **concatenates** sources; there is no import graph. |
| [build_and_test.md](build_and_test.md) | CMake configure/build, CTest, typical env vars. | **`./nexc.sh check`** wraps configure+build+ctest; raw commands are the portable contract. |
| [inspection_modes.md](inspection_modes.md) | `--dump-*`, `--check`, what each stage prints. | Full `--dump-mlir` / `--dump-llvm` need the matching LLVM/MLIR tools **found at CMake configure time**. |
| [diagnostics.md](diagnostics.md) | Diagnostic shape, severities, exit codes. | Exit code **2** is CLI misuse; **1** is compile/runtime failure of the driver. |
| [artifacts.md](artifacts.md) | `build/nexc`, `libnexrt.a`, stdout dumps vs temp files for native compile. | Native compile writes **temporary LLVM IR** before `llc` / `ld.lld` unless you only dump text to stdout. |

## Not Yet Documented Here

| Topic | Status |
| ----- | ------ |
| `portability.md` (hosts, OS tiers) | Planned; Linux-first native link today. |

## Principles

- **`build/nexc`** is the primary interface contract.
- **`nexc.sh`** is convenience automation, not a second source of truth for flags.
- Document behavior users can rely on; point to code only for intentional extension hooks.
