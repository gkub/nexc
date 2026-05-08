# NEXC LLM Reference

Use this file as the first stop when handing the project to a new chat or LLM.
It is an index, not a full spec.

## Current Project State

`nexc` is a C++ compiler project for the nex language. The current implementation
is a **checked Core v0 frontend**:

```text
source -> lexer -> tokens -> parser -> AST -> semantic analysis
```

Current frontend capabilities:

- token dumping: `./nexc.sh tokens <file.nexs>`
- AST text dumping: `./nexc.sh ast <file.nexs>`
- AST Graphviz DOT dumping: `./nexc.sh ast-dot <file.nexs>`
- AST Graphviz DOT + SVG generation: `./nexc.sh ast-graph <file.nexs> [prefix]`
- semantic checking: `./nexc.sh check-file <file.nexs>`

Not implemented yet:

- native code generation
- MLIR/LLVM lowering
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
| Optimization / analysis direction | [docs/design/optimization_goals.md](./docs/design/optimization_goals.md) |
| Lexer/parser/AST/semantic headers | [include/nexc/frontend/](./include/nexc/frontend/) |
| Frontend implementation | [src/frontend/](./src/frontend/) |
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
- parser-negative fixtures
- semantic success fixtures
- semantic-negative fixtures

Run them with:

```sh
./nexc.sh check
```

## Near-Term Direction

The next major discussion is backend/IR strategy. Options to consider:

1. A tiny nex typed IR before MLIR/LLVM.
2. Direct textual LLVM IR for a very small subset.
3. First MLIR generation for Core v0 scalar programs.

Before or during that, keep docs updated:

- Update [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) when adding compiler concepts.
- Update [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) when user-visible nex syntax changes.
- Update [docs/language/core_v0.md](./docs/language/core_v0.md) when normative language behavior changes.
- Update tests/goldens with the implementation change that intentionally alters behavior.

## Style Guidance For Future Agents

- Do not jump to MLIR/LLVM before checking current frontend assumptions.
- Keep parser syntax checks separate from semantic meaning checks.
- Prefer small, well-documented compiler stages.
- Add tests with every behavior change.
- Keep `NEXC_HOLY_BOOK.md` educational, not just a changelog.
- Keep user docs free of compiler-internal jargon unless it helps explain an error.
