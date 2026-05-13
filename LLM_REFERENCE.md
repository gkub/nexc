# NEXC LLM Reference

First stop when opening this repo in a new chat or tool: **pointers and vocabulary**, not a language spec. Normative behavior lives under **`docs/reference/`**; sequencing lives in **`docs/IMPLEMENTATION_BACKLOG.md`**.

## Pipeline and entrypoints

```text
source → lexer → parser → AST → semantic analysis → typed IR → MLIR → LLVM IR → native executable
```

- **Primary binary:** `build/nexc` (configure with **`MLIR_DIR`** pointing at your MLIR CMake package; see [README.md](./README.md) Development Setup).
- **Wrapper:** `./nexc.sh` configures, builds, and runs CTest; also exposes common dump/check commands.

Human-oriented tour of dump vs compile: [NEXC_HOLY_BOOK.md §1 Current Pipeline](./NEXC_HOLY_BOOK.md#1-current-pipeline). Non-trivial single-file pipeline example: [examples/pipeline_walkthrough.nexs](./examples/pipeline_walkthrough.nexs).

## What exists today (summary)

Inspection and compile modes (details and flags: [compiler_cli.md](./docs/reference/toolchain/compiler_cli.md), [inspection_modes.md](./docs/reference/toolchain/inspection_modes.md)):

- `--dump-tokens`, `--dump-ast`, `--dump-ast-dot`, `--check`
- `--dump-ir`, `--dump-mlir`, `--dump-llvm`
- `nexc <one-or-more.nexs>... -o <output>`: sources concatenated in order, parsed as **one** translation unit, then lowered to a host executable when native linking was enabled at CMake configure time.

**Control flow:** `if` / `else`, `while`, C-style **`for`** (typed IR desugars `for` to `while` plus an optional step block), **`break`**, **`continue`**.

**I/O and built-ins:** formatted **`print` / `println`** (literal format string with `{}` placeholders), legacy-style `println` with a `str` argument where still allowed, **`readln() -> str`**, **`parse_*`**, **`input_ok()`**. Full rules: [builtins_and_io.md](./docs/reference/language/builtins_and_io.md). Runtime implementation: [runtime/nex_runtime.c](./runtime/nex_runtime.c) (today Linux-oriented syscalls for I/O helpers).

**Types and data:** scalars (`i8`…`u64`, `bool`, `void`, `str`), `let` / `let mut`, calls, assignments. **Fixed-size arrays** `[T; N]` for **local** bindings only (literals, index read, `mut` element assign); not parameters, return types, or module `const` yet ([types.md](./docs/reference/language/types.md)).

**Logical operators:** `&&` and `||` are **eager** (no short-circuit yet); see [expressions.md](./docs/reference/language/expressions.md).

**Native link:** `nexc` writes temporary LLVM IR, runs **`llc`** to a relocatable object, then **Linux:** **`ld.lld`** with discovered glibc CRT paths; **macOS:** host **`clang`** as Mach-O link driver. Requires matching tools at CMake configure time ([README](./README.md), [Holy Book §14](./NEXC_HOLY_BOOK.md#14-native-executable-driver)). No in-process codegen yet (still subprocess-driven).

**Not in scope yet (examples):** imports / modules / headers, user-defined structs beyond what exists today, heap allocation, channels/tasks, short-circuiting `&&`/`||`.

## Build and test

```sh
cmake -S . -B build -DMLIR_DIR=<path-to-mlir>/lib/cmake/mlir
cmake --build build
ctest --test-dir build --output-on-failure
```

Or `./nexc.sh check` for the usual local loop. Canonical workflows: [build_and_test.md](./docs/reference/toolchain/build_and_test.md).

## Where to look (index)

| Need | Start here |
| ---- | ----------- |
| Map of `docs/` tree | [docs/README.md](./docs/README.md) |
| Repo overview, setup | [README.md](./README.md) |
| Long-horizon language/compiler narrative | [nex.md](./nex.md) |
| **Ordered next milestones** | [docs/IMPLEMENTATION_BACKLOG.md](./docs/IMPLEMENTATION_BACKLOG.md) |
| **Normative “what the compiler does now”** | [docs/reference/README.md](./docs/reference/README.md), then [language/README](./docs/reference/language/README.md), [toolchain/README](./docs/reference/toolchain/README.md), [runtime/README](./docs/reference/runtime/README.md) |
| Formal CLI | [compiler_cli.md](./docs/reference/toolchain/compiler_cli.md) |
| Learn to write programs | [docs/user/tutorial.md](./docs/user/tutorial.md) |
| Inspection / `nexc.sh` UX | [frontend.md](./docs/user/frontend.md) |
| Compiler walkthrough | [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) |
| Design drafts (non-normative) | [docs/design/](./docs/design/) (e.g. [frontend_contract.md](./docs/design/frontend_contract.md), [llvm_native_first_slice.md](./docs/design/llvm_native_first_slice.md)) |
| Frontend headers / sources | [include/nexc/frontend/](./include/nexc/frontend/), [src/frontend/](./src/frontend/) |
| Typed IR | [include/nexc/ir/](./include/nexc/ir/), [src/ir/](./src/ir/) |
| MLIR lowering | [include/nexc/mlir/](./include/nexc/mlir/), [src/mlir/](./src/mlir/) |
| LLVM IR export, native driver | [src/llvm/](./src/llvm/), [src/tools/nexc/main.cpp](./src/tools/nexc/main.cpp) |
| Examples and tests | [examples/](./examples/), [tests/](./tests/), [tests/golden/](./tests/golden/), [CMakeLists.txt](./CMakeLists.txt) |
| CI | [.github/workflows/ci.yml](./.github/workflows/ci.yml) |

## Tests (CTest)

Registered in [CMakeLists.txt](./CMakeLists.txt): golden dumps (tokens through LLVM), optional **`mlir-opt`** verification when MLIR tools exist, optional **`llvm-as`** on LLVM text, **native compile-and-run** tests when the host link recipe was configured (Linux `llc`+`ld.lld`, macOS `llc`+`clang`), semantic positive/negative fixtures, parser negatives.

## Near-term direction

Follow the backlog order (today: definite assignment / uninitialized locals, then tightening fixed arrays, pointers later). Prefer updating **`docs/reference/`** and goldens with each behavior change. High-level roadmap bullets also appear under [README.md § Forward Priorities](./README.md#forward-priorities).

When editing docs:

- **Normative behavior:** update the right file under `docs/reference/` (and tests), not only design drafts.
- **Holy Book:** add or adjust teaching sections when pipeline stages gain new user-visible behavior.

## Style guidance for agents

### Implementation and tests

- Do not skip straight to MLIR/LLVM without confirming frontend and typed IR assumptions.
- Keep parser syntax checks separate from semantic checks.
- Prefer small, well-explained compiler stages; new structures in headers and non-trivial `.cpp` files should carry comments that say what they are and how they connect to adjacent stages (this is an educational codebase, not silent production style).
- Add or extend tests with every intentional behavior change.
- Keep `NEXC_HOLY_BOOK.md` explanatory, not only a changelog.

### Documentation tone

- State **what is true** now; avoid vague comparisons to other toolchains unless the contrast is the teaching point.
- Avoid paragraphs that only justify a past chat decision; replace with inputs, outputs, and failure modes maintainers need.
- Define abbreviations once (example: **CRT**: C run-time startup objects used in the native link recipe on Linux), then use the short form.
