# nexc

`nexc` is the reference compiler for **nex**, a lightweight systems programming language focused on explicit costs, predictable execution, concurrency visibility, realtime-safe regions, mathematical clarity, and native code generation.

Core slogan:

> Nothing expensive is implicit.

nex is currently in an early compiler implementation stage. The project remains
docs-first: syntax, semantics, examples, and design constraints should be defined
before or alongside implementation.

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

1. Keep Core v0 syntax and semantics well documented
2. Maintain a checked frontend: lexer, parser, AST, semantic analysis
3. Grow the nex-owned typed IR with golden tests
4. Lower small typed IR slices into MLIR
5. Add LLVM/native lowering after MLIR lowering is clearer
6. Build nex-specific analyses and optimizations incrementally

## Current Status

The compiler currently supports:

- token dumps
- AST text and Graphviz DOT dumps
- semantic checking for Core v0 examples
- typed IR dumps
- MLIR lowering for Core v0 scalars/control flow: fixed-width integers,
booleans, module constants, mutable local storage, assignment, function calls,
`if`/`else`, `while`, `/`, `%`, unsigned comparisons/division/remainder, and
eager `&&` / `||`
- string literal lowering to global bytes plus length
- LLVM IR dumping with `llvm-as` validation when LLVM tools are available
- native executable generation using `clang` as the host linker/codegen driver
- runtime-backed `print(str)` and `println(str)` for observable stdout
- an experimental `readln() -> str` stdin slice for direct input/output examples

For example, this now builds and prints real output:

```sh
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

The runtime is now built as `build/runtime/libnexrt.a` and discovered relative to
the `nexc` executable. Set `NEXC_RUNTIME_LIBRARY` only if you are testing an
unusual runtime location.

## Development Setup

On Ubuntu 24.04, install the basic build tools plus LLVM/MLIR 18:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential clang graphviz
sudo apt-get install -y libmlir-18-dev mlir-18-tools
```

The LLVM/MLIR packages install headers, CMake config files, libraries, and tools under
`/usr/lib/llvm-18`. Adding the LLVM tools directory to the shell `PATH` makes
commands such as `mlir-opt`, `mlir-translate`, and `llvm-as` available directly:

```sh
echo 'export PATH=/usr/lib/llvm-18/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
mlir-opt --version
```

After building the compiler, you can use `build/nexc` directly to compile a nex
program without the helper script:

```sh
build/nexc examples/pipeline_walkthrough.nexs -o build/pipeline_walkthrough
```

Official setup references:

- [MLIR Getting Started](https://mlir.llvm.org/getting_started/)
- [LLVM Getting Started](https://llvm.org/docs/GettingStarted.html)
- [Building LLVM with CMake](https://llvm.org/docs/CMake.html)

For non-Ubuntu systems, use the official LLVM/MLIR installation or source-build
instructions. The important requirement for this project is that CMake can find
the MLIR package, usually via `MLIR_DIR`, and that MLIR tools such as `mlir-opt`
are available for validation.

## Quick Start

Use the root developer script for day-to-day work:

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
docs/       Language specification and design notes
  language/ Normative language slices (e.g. Core v0)
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
- [docs/language/core_v0.md](./docs/language/core_v0.md) - normative **Core v0** language (first compiler milestone)
- [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) - beginner guide for writing current nex programs
- [examples/pipeline_walkthrough.nexs](./examples/pipeline_walkthrough.nexs) - nontrivial frontend pipeline walkthrough input
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

Early-stage. The frontend, typed IR, MLIR/LLVM IR dumps, and scalar native
executable path are implemented, but runtime-backed features are still missing.

## License

TBD.