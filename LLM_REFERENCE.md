# NEXC LLM Reference

Use this file as the first stop when handing the project to a new chat or LLM.
It is an index, not a full spec.

## Current Project State

`nexc` is a C++ compiler project for the nex language. The current implementation
is a **checked Core v0 compiler path** with typed IR, MLIR, LLVM IR, and native
execution for the documented v0 feature set:

```text
source -> lexer -> parser -> AST -> semantic analysis -> typed IR -> MLIR -> LLVM IR -> native executable
```

For the human-oriented explanation of build vs dump vs compile flow, start with
[NEXC_HOLY_BOOK.md §1 Current Pipeline](./NEXC_HOLY_BOOK.md#1-current-pipeline).
The main walkthrough input is
[examples/pipeline_walkthrough.nexs](./examples/pipeline_walkthrough.nexs).

Current frontend capabilities:

- token dumping: `./nexc.sh tokens <file.nexs>`
- AST text dumping: `./nexc.sh ast <file.nexs>`
- AST Graphviz DOT dumping: `./nexc.sh ast-dot <file.nexs>`
- AST Graphviz DOT + SVG generation: `./nexc.sh ast-graph <file.nexs> [prefix]`
- semantic checking: `./nexc.sh check-file <file.nexs>`
- typed IR dumping: `./nexc.sh ir <file.nexs>`
- first MLIR dumping: `./nexc.sh mlir <file.nexs>`
- LLVM IR dumping: `./nexc.sh llvm <file.nexs>`
- direct executable compilation: `build/nexc <file.nexs> -o <output>`

Still outside Core v0:

- arrays, imports/modules, user-defined types, allocation, channels, tasks
- formatting/interpolation and general I/O APIs beyond stdout printing
- direct object-file emission without delegating final codegen/linking to `clang`

`print(str)` and `println(str)` are semantic built-ins that now lower to the tiny
bootstrap runtime in `runtime/nex_runtime.c`. `readln() -> str` is the first
experimental stdin built-in; it is intended for direct use such as
`println(readln());` while string ownership is still provisional.

## Build And Test

Use the root helper script:

```sh
./nexc.sh check
```

This configures CMake, builds `nexc`, and runs CTest.

Useful one-file commands:

```sh
./nexc.sh check-file examples/hello.nexs
./nexc.sh tokens examples/hello.nexs
./nexc.sh ast examples/hello.nexs
./nexc.sh ir examples/hello.nexs
./nexc.sh mlir examples/comparison.nexs
./nexc.sh ast-graph examples/hello.nexs
```

## Where To Look

| Topic | File |
| ----- | ---- |
| Project overview / long-term vision | [nex.md](./nex.md) |
| Public README / quick start | [README.md](./README.md) |
| How to build/test/run frontend tools | [nexc.sh](./nexc.sh), [docs/user/frontend.md](./docs/user/frontend.md) |
| How to write current nex code | [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) |
| Normative Core v0 language rules | [docs/language/core_v0.md](./docs/language/core_v0.md) |
| Educational compiler guide | [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) |
| Frontend implementation contract | [docs/design/frontend_contract.md](./docs/design/frontend_contract.md) |
| Core v0 typed IR design note | [docs/design/core_v0_typed_ir.md](./docs/design/core_v0_typed_ir.md) |
| First LLVM/native slice | [docs/design/llvm_native_first_slice.md](./docs/design/llvm_native_first_slice.md) |
| I/O design notes | [docs/design/io_v1_spitball.md](./docs/design/io_v1_spitball.md) |
| Arrays/vectors/linear algebra notes | [docs/design/arrays_vectors_linalg.md](./docs/design/arrays_vectors_linalg.md) |
| Optimization / analysis direction | [docs/design/optimization_goals.md](./docs/design/optimization_goals.md) |
| Lexer/parser/AST/semantic headers | [include/nexc/frontend/](./include/nexc/frontend/) |
| Frontend implementation | [src/frontend/](./src/frontend/) |
| Typed IR model, builder, dumper | [include/nexc/ir/](./include/nexc/ir/), [src/ir/](./src/ir/) |
| MLIR lowering and textual dump API | [include/nexc/mlir/](./include/nexc/mlir/), [src/mlir/](./src/mlir/) |
| CLI driver | [src/tools/nexc/main.cpp](./src/tools/nexc/main.cpp) |
| Example nex programs | [examples/](./examples/) |
| Nontrivial pipeline walkthrough input | [examples/pipeline_walkthrough.nexs](./examples/pipeline_walkthrough.nexs) |
| Invalid/semantic test fixtures | [tests/](./tests/) |
| Golden expected output | [tests/golden/](./tests/golden/) |
| CI workflow | [.github/workflows/ci.yml](./.github/workflows/ci.yml) |

## Current Language Surface

Core v0 currently supports:

- top-level `fn` and module-level `const`
- `main() -> void` or `main() -> i32`
- integer types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`
- `bool`, `str`, `void`
- string literals
- `let` and `let mut`
- assignment to mutable locals
- arithmetic, comparison, equality, `&&`, `||`, `!`
- `if` / `else`
- `while`
- function calls
- built-in `print(str) -> void` and `println(str) -> void`
- experimental built-in `readln() -> str`

Important semantic note: current Core v0 `&&` and `||` are eager boolean
operators. They do not short-circuit yet.

See [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) for the
friendly version, and [docs/language/core_v0.md](./docs/language/core_v0.md)
for the more precise version.

## Current Tests

Tests are CTest entries defined in [CMakeLists.txt](./CMakeLists.txt).

Current categories:

- smoke tests for token/AST dumping
- golden output tests for tokens, AST, Graphviz DOT, and selected diagnostics
- golden output tests for typed IR, MLIR, and LLVM dumps
- conditional `mlir-opt` validation tests for generated MLIR when MLIR tools are
  available
- conditional `llvm-as` validation and native executable tests when LLVM/clang
  are available
- parser-negative fixtures
- semantic success fixtures
- semantic-negative fixtures

Run them with:

```sh
./nexc.sh check
```

## Near-Term Direction

Near-term direction after Core v0 is design work, not surprise syntax changes:
spitball proper Nex I/O APIs (files, stdin/stdout/stderr, pipes) before adding
surface syntax, then grow beyond v0 with arrays, modules, allocation, and richer
runtime services.

Before or during that, keep docs updated:

- Update [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) when adding compiler concepts.
- Update [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) when user-visible nex syntax changes.
- Update [docs/language/core_v0.md](./docs/language/core_v0.md) when normative language behavior changes.
- Update tests/goldens with the implementation change that intentionally alters behavior.

## Style Guidance For Future Agents

- Do not jump to MLIR/LLVM before checking current frontend assumptions.
- Keep parser syntax checks separate from semantic meaning checks.
- Prefer small, well-documented compiler stages.
- Treat this compiler as an educational resource first: public headers and
  nontrivial source files should have thorough comments explaining what each
  compiler concept is, why it exists, and how it connects to the surrounding
  stage. Do not leave new compiler structures as uncommented production-style
  data bags.
- Add tests with every behavior change.
- Keep `NEXC_HOLY_BOOK.md` educational, not just a changelog.
- Keep user docs free of compiler-internal jargon unless it helps explain an error.
