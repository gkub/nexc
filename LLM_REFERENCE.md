# NEXC LLM Reference

Use this file as the first stop when handing the project to a new chat or LLM.
It is an index, not a full spec.

## Current Project State

`nexc` is a C++ compiler project for the nex language. The current implementation
is a **checked Core v0 frontend with typed IR dumps and first scalar/control-flow
MLIR lowering**:

```text
source -> lexer -> tokens -> parser -> AST -> semantic analysis -> typed IR -> MLIR
```

Current frontend capabilities:

- token dumping: `./nexc.sh tokens <file.nexs>`
- AST text dumping: `./nexc.sh ast <file.nexs>`
- AST Graphviz DOT dumping: `./nexc.sh ast-dot <file.nexs>`
- AST Graphviz DOT + SVG generation: `./nexc.sh ast-graph <file.nexs> [prefix]`
- semantic checking: `./nexc.sh check-file <file.nexs>`
- typed IR dumping: `./nexc.sh ir <file.nexs>`
- first MLIR dumping: `./nexc.sh mlir <file.nexs>`

Not implemented yet:

- native code generation
- complete MLIR lowering beyond simple scalar returns, arithmetic, integer
  comparisons, direct function calls, and returning `if`/`else`
- LLVM lowering
- runtime execution
- real `print` / `println` output at runtime
- arrays, imports/modules, user-defined types, allocation, channels, tasks

`print(str)` and `println(str)` are currently **semantic built-ins** only. They
type-check, but they do not run until lowering/runtime support exists.

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
| Optimization / analysis direction | [docs/design/optimization_goals.md](./docs/design/optimization_goals.md) |
| Lexer/parser/AST/semantic headers | [include/nexc/frontend/](./include/nexc/frontend/) |
| Frontend implementation | [src/frontend/](./src/frontend/) |
| Typed IR model, builder, dumper | [include/nexc/ir/](./include/nexc/ir/), [src/ir/](./src/ir/) |
| MLIR lowering and textual dump API | [include/nexc/mlir/](./include/nexc/mlir/), [src/mlir/](./src/mlir/) |
| CLI driver | [src/tools/nexc/main.cpp](./src/tools/nexc/main.cpp) |
| Example nex programs | [examples/](./examples/) |
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

See [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) for the
friendly version, and [docs/language/core_v0.md](./docs/language/core_v0.md)
for the more precise version.

## Current Tests

Tests are CTest entries defined in [CMakeLists.txt](./CMakeLists.txt).

Current categories:

- smoke tests for token/AST dumping
- golden output tests for tokens, AST, Graphviz DOT, and selected diagnostics
- golden output tests for typed IR and MLIR dumps
- conditional `mlir-opt` validation tests for generated MLIR when MLIR tools are
  available
- parser-negative fixtures
- semantic success fixtures
- semantic-negative fixtures

Run them with:

```sh
./nexc.sh check
```

## Near-Term Direction

The next major implementation direction is lowering from the tiny typed IR. See
[docs/design/core_v0_typed_ir.md](./docs/design/core_v0_typed_ir.md).

Recommended next slice:

1. Keep IR golden tests growing as Core v0 grows.
2. Grow MLIR beyond returning `if`/`else` toward mutable locals, loop lowering,
   module constants, and runtime calls.
3. Keep MLIR output covered by golden tests and conditional `mlir-opt`
   validation.
4. Add runtime strategy only after scalar lowering is clear.

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
