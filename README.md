# nexc

`nexc` is the reference compiler for **nex**, a lightweight systems programming
language focused on explicit costs, predictable execution, visible resource flow,
and mathematically serious systems programming.

nex is currently in an early compiler implementation stage. The project remains
docs-first: syntax, semantics, examples, and design constraints should be defined
before or alongside implementation.

## Core Principles

- Explicit costs over hidden work
- Predictable execution and visible control flow
- Concurrency and effects that remain inspectable
- Embedded/realtime constraints considered from the start
- A mathematically serious path toward shape-aware linear algebra
- Own the language/runtime boundary (I/O included), even when bootstrapping with host tools

## Project Goals

- Build a serious educational compiler project
- Implement the compiler in modern C++
- Use a hand-written lexer and parser
- Lower through MLIR and LLVM
- Generate native code rather than transpiling to C
- Support RISC-V as a serious future target
- Keep embedded and realtime constraints in mind from the start
- Support compiler-visible math abstractions such as shape-aware linear algebra
- Explore nex-specific optimization problems around effects, resources, copies, regions, and realtime execution
- Eventually explore partial or full self-hosting

## Current Focus

1. Keep **`docs/reference/`** and [`docs/IMPLEMENTATION_BACKLOG.md`](./docs/IMPLEMENTATION_BACKLOG.md) aligned with the compiler
2. Maintain a checked frontend: lexer, parser, AST, semantic analysis
3. Grow the nex-owned typed IR with golden tests
4. Lower typed IR slices into MLIR / LLVM and ship native executables
5. Build nex-specific analyses and optimizations incrementally

## Current Status

The compiler currently supports:

- token dumps
- AST text and Graphviz DOT dumps
- semantic checking for the example corpus and tests
- typed IR dumps
- MLIR lowering for scalars/control flow: fixed-width integers,
booleans, module constants, mutable local storage, assignment, function calls,
`if`/`else`, `while`, **`for`** (desugared to `while` in typed IR), `/`, `%`,
unsigned comparisons/division/remainder, and eager `&&` / `||`
- string literal lowering to global bytes plus length
- LLVM IR dumping with `llvm-as` validation when LLVM tools are available
- native executable generation: `llc` (IR to object), then Linux uses `ld.lld`
  with glibc CRT paths at CMake configure time; macOS uses the host `clang` driver
  to link Mach-O (`llc` from LLVM is still required; Xcode alone may not provide it)
- runtime-backed formatted **`print` / `println`** and stdout helpers
- an experimental `readln() -> str` stdin slice for direct input/output examples
- explicit input parsing helpers: `parse_i32`, `parse_u64`, `parse_bool`, and
  `input_ok()`

For example, this now builds and prints real output:

```sh
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

The runtime is built as `build/runtime/libnexrt.a` and discovered relative to the
`nexc` executable. Set `NEXC_RUNTIME_LIBRARY` only if you are testing an unusual
runtime location. Current runtime I/O internals are Linux-first (`read(2)` /
`write(2)`), which keeps us off stdio while preserving the same Nex-level
built-in behavior.

## Development Setup

### Ubuntu 24.04

Install the basic build tools plus LLVM/MLIR 18:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential clang graphviz
sudo apt-get install -y libmlir-18-dev mlir-18-tools lld-18
```

The LLVM/MLIR packages install headers, CMake config files, libraries, and tools under
`/usr/lib/llvm-18`. Adding the LLVM tools directory to the shell `PATH` makes
commands such as `mlir-opt`, `mlir-translate`, and `llvm-as` available directly:

```sh
echo 'export PATH=/usr/lib/llvm-18/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
mlir-opt --version
```

Configure and build (from the repository root):

```sh
cmake -S . -B build -DMLIR_DIR=/usr/lib/llvm-18/lib/cmake/mlir
cmake --build build
```

### macOS (Homebrew)

Install Xcode Command Line Tools (host **`clang`** for linking): `xcode-select --install`.

Install CMake, Ninja, LLVM/MLIR tools, and Graphviz:

```sh
brew install cmake ninja llvm graphviz
```

Put Homebrew’s LLVM **`bin`** on `PATH` so `mlir-opt`, `llvm-as`, and **`llc`**
resolve to the same install you pass as **`MLIR_DIR`**:

```sh
export PATH="$(brew --prefix llvm)/bin:$PATH"
cmake -S . -B build -DMLIR_DIR="$(brew --prefix llvm)/lib/cmake/mlir"
cmake --build build
```

Verify: `command -v mlir-opt llc llvm-as` and `mlir-opt --version`. CMake records
absolute paths to `llc` and the link driver at configure time; re-run `cmake` if
you upgrade or move the LLVM prefix.

After building the compiler, you can use `build/nexc` directly to compile a nex
program without the helper script:

```sh
build/nexc examples/pipeline_walkthrough.nexs -o build/pipeline_walkthrough
```

Official setup references:

- [MLIR Getting Started](https://mlir.llvm.org/getting_started/)
- [LLVM Getting Started](https://llvm.org/docs/GettingStarted.html)
- [Building LLVM with CMake](https://llvm.org/docs/CMake.html)

On Linux distributions other than Ubuntu, or if Homebrew’s LLVM version does not
match what `find_package(MLIR)` expects, use the official LLVM/MLIR packages or
source-build instructions. The project requires a discoverable MLIR CMake package
(typically `MLIR_DIR`) and tools such as `mlir-opt` and `llvm-as` on `PATH` for
tests.

## Quick Start

`build/nexc` is the primary compiler interface. `nexc.sh` is a convenience
wrapper for common developer loops.

Build and run the compiler directly:

```sh
cmake -S . -B build
cmake --build build
build/nexc --check examples/hello.nexs
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

Use the helper script for day-to-day development loops:

```sh
./nexc.sh check
```

That configures CMake, builds the compiler, and runs the CTest suite.
When MLIR and `mlir-opt` are available, the test suite also validates generated
MLIR modules with MLIR's verifier.

Useful inspection commands:

```sh
./nexc.sh tokens examples/minimal.nexs
./nexc.sh check-file examples/hello.nexs
./nexc.sh ast examples/add.nexs
./nexc.sh ir examples/add.nexs
./nexc.sh mlir examples/comparison.nexs
./nexc.sh llvm examples/return_42.nexs
./nexc.sh ast-graph examples/add.nexs
```

That writes `ast.dot` and `ast.svg` if Graphviz `dot` is installed. To choose a
different output prefix:

```sh
./nexc.sh ast-graph examples/add.nexs build/add_ast
```

## Planned Compiler Pipeline

For a hand-held explanation of the difference between building `nexc`, dumping
compiler stages, and eventually compiling a nex program, see
[NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md#1-current-pipeline).

Source code  
→ Lexer  
→ Parser  
→ AST  
→ Semantic analysis  
→ typed IR  
→ MLIR generation  
→ MLIR lowering  
→ LLVM IR  
→ Native code

## Repository Layout

```txt
docs/       Language specification, design notes, implementation backlog
  IMPLEMENTATION_BACKLOG.md  Ordered near-term milestones (source of truth)
  language/ Frozen language snapshots (see also docs/reference/)
  design/   Architecture, optimization, implementation strategy
examples/   Example nex programs
src/        Compiler implementation
include/    Public/internal C++ headers
  runtime/    Tiny bootstrap runtime linked into native executables
tests/      Compiler tests
nex.md      Living project overview and AI/context document
```

## Key Documents

- [LLM_REFERENCE.md](./LLM_REFERENCE.md) - compact index for future chats/LLMs
- [docs/IMPLEMENTATION_BACKLOG.md](./docs/IMPLEMENTATION_BACKLOG.md) - ordered next milestones (for, def-assign, arrays, pointers, …)
- [docs/reference/README.md](./docs/reference/README.md) - long-lived user reference spine (language/toolchain/runtime)
- [docs/reference/toolchain/compiler_cli.md](./docs/reference/toolchain/compiler_cli.md) - formal `build/nexc` CLI reference
- [docs/reference/toolchain/build_and_test.md](./docs/reference/toolchain/build_and_test.md) - canonical build and test workflows
- [docs/reference/toolchain/inspection_modes.md](./docs/reference/toolchain/inspection_modes.md) - `--dump-*`, `--check`, and compile mode behavior
- [docs/reference/toolchain/diagnostics.md](./docs/reference/toolchain/diagnostics.md) - diagnostic format and error categories
- [docs/reference/toolchain/artifacts.md](./docs/reference/toolchain/artifacts.md) - build/runtime/inspection artifact expectations
- [docs/reference/language/builtins_and_io.md](./docs/reference/language/builtins_and_io.md) - built-ins and current I/O behavior
- [docs/reference/language/expressions.md](./docs/reference/language/expressions.md) - expression forms and operator behavior
- [docs/reference/language/types.md](./docs/reference/language/types.md) - current type system surface
- [docs/reference/language/statements.md](./docs/reference/language/statements.md) - current statement forms and rules
- [docs/reference/language/declarations_and_modules.md](./docs/reference/language/declarations_and_modules.md) - translation-unit and top-level declaration rules
- [docs/reference/language/functions_and_calls.md](./docs/reference/language/functions_and_calls.md) - function signatures, calls, and built-ins
- [docs/language/core_v0.md](./docs/language/core_v0.md) - frozen **early language** snapshot (historical baseline)
- [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) - beginner guide for writing current nex programs
- [examples/pipeline_walkthrough.nexs](./examples/pipeline_walkthrough.nexs) - nontrivial frontend pipeline walkthrough input
- [examples/for_loop.nexs](./examples/for_loop.nexs) - C-style `for` loop (sums 0..9)
- [docs/design/frontend_contract.md](./docs/design/frontend_contract.md) - lexer/parser/AST contract for the first frontend implementation
- [docs/design/core_v0_typed_ir.md](./docs/design/core_v0_typed_ir.md) - design note for the first backend-facing typed IR
- [docs/design/llvm_native_first_slice.md](./docs/design/llvm_native_first_slice.md) - first LLVM/native lowering milestone plan
- [docs/design/io_v1_spitball.md](./docs/design/io_v1_spitball.md) - stdin/files/pipes design notes
- [docs/design/arrays_vectors_linalg.md](./docs/design/arrays_vectors_linalg.md) - arrays/vectors design notes for future linear algebra
- [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) - educational guide to the compiler as it grows
- [docs/user/frontend.md](./docs/user/frontend.md) - practical guide for frontend inspection and semantic checking
- [nex.md](./nex.md) - living project overview and language-planning context
- [docs/design/optimization_goals.md](./docs/design/optimization_goals.md) - optimization and static-analysis goals

## Language Direction

nex is intended to support:

- explicit allocation and copying
- explicit blocking and concurrency
- task/channel-based concurrency
- fixed-size arrays and buffers
- shape-aware linear algebra
- optional resource observability
- realtime-safe function regions
- compiler-visible effect tracking
- low-overhead embedded-oriented profiles

nex is not intended to initially support:

- garbage collection
- classes or inheritance
- exceptions
- async/await
- macros
- templates
- advanced metaprogramming
- hidden runtime behavior

## Implementation Language

The initial compiler is written in **C++**, primarily because LLVM and MLIR are C++ ecosystems and the project is intended to build practical systems/compiler experience.

Long term, nex may become partially or fully self-hosting once the language is mature enough.

## Status

Early-stage, but the baseline pipeline is implemented end-to-end:

- checked frontend (lexer/parser/semantic analysis)
- typed IR dumps
- MLIR and LLVM IR dumps with validation tests
- native executable generation through `build/nexc <file.nexs> -o <output>`
- runtime-backed stdout (`print` / `println`)
- experimental stdin slice (`readln() -> str`) plus explicit parse helpers

## Next Documentation Step

User-facing rules are migrating into **`docs/reference/`** (language, toolchain,
runtime). Treat older milestone prose under `docs/language/` as snapshots unless
explicitly updated.

## Forward Priorities

0. **Near-term language/compiler sequencing** — single living checklist:
   [`docs/IMPLEMENTATION_BACKLOG.md`](./docs/IMPLEMENTATION_BACKLOG.md)
   (**`for` shipped** → uninitialized locals + definite assignment → tighten arrays;
   **pointers** tracked as a later mega-item; doc and test expectations per wave).
1. Build a proper long-lived user reference (language, toolchain, runtime/std),
   replacing milestone-centric docs as the main user-facing source of truth.
2. Design and implement a real Nex-owned I/O model (stdin/stdout/stderr, files,
   pipes) with explicit effects/costs and clear error behavior.
3. Stabilize string/slice/resource semantics needed by practical I/O APIs.
4. Add fixed-size arrays first, then slices/views, then growable vectors after
   allocation/ownership are ready.
5. Evolve toward shape-aware linear algebra as a first-class design target, not
   an afterthought.
6. Keep compiler/runtime portability in mind from day one (Linux-first is fine),
   with RISC-V and embedded constraints as active design inputs.

## License

TBD.