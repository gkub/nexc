# Toolchain Reference

This section documents how users interact with the compiler and related tooling.

## Planned Pages

- `compiler_cli.md` (`build/nexc` command surface and flags) (created)
- `build_and_test.md` (CMake/CTest flow and helper script role) (created)
- `inspection_modes.md` (`--dump-*`, `--check`, output contracts) (created)
- `diagnostics.md` (format, spans, caret output, exit codes) (created)
- `artifacts.md` (IR/MLIR/LLVM dumps, executable outputs, temp files) (created)
- `portability.md` (host assumptions, platform support status)

## Principles

- `build/nexc` is the primary interface.
- `nexc.sh` is convenience automation.
- Tool behavior should be documented as contracts, not implementation accidents.
