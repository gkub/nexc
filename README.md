# nexc

`nexc` is the reference compiler for **NEX**, a lightweight systems programming language focused on explicit costs, predictable execution, concurrency visibility, realtime-safe regions, mathematical clarity, and native code generation.

Core slogan:

> Nothing expensive is implicit.

NEX is currently in the **language-definition and compiler-planning stage**. The project is intentionally docs-first: syntax, semantics, examples, and design constraints should be defined before implementation.

## Project Goals

- Build a serious educational compiler project
- Implement the compiler in modern C++
- Use a hand-written lexer and parser
- Lower through MLIR and LLVM
- Generate native code rather than transpiling to C
- Support RISC-V as a serious future target
- Keep embedded and realtime constraints in mind from the start
- Support compiler-visible math abstractions such as shape-aware linear algebra
- Explore NEX-specific optimization problems around effects, resources, copies, regions, and realtime execution
- Eventually explore partial or full self-hosting

## Current Focus

1. Define the NEX language model
2. Document core syntax and semantics
3. Design the compiler architecture
4. Implement the frontend incrementally
5. Add semantic analysis and effect tracking
6. Add MLIR/LLVM lowering after the frontend is solid
7. Build NEX-specific analyses and optimizations incrementally

## Quick Start

Use the root developer script for day-to-day work:

```sh
./nexc.sh check
```

That configures CMake, builds the compiler, and runs the CTest suite.

Useful inspection commands:

```sh
./nexc.sh tokens examples/minimal.nexs
./nexc.sh check-file examples/hello.nexs
./nexc.sh ast examples/add.nexs
./nexc.sh ast-dot examples/add.nexs > ast.dot
./nexc.sh check-file examples/add.nexs
```

Render the Graphviz output if `dot` is installed:

```sh
dot -Tsvg ast.dot -o ast.svg
```

## Planned Compiler Pipeline

Source code  
→ Lexer  
→ Parser  
→ AST  
→ Semantic analysis  
→ MLIR generation  
→ MLIR lowering  
→ LLVM IR  
→ Native code

## Repository Layout

```txt
docs/       Language specification and design notes
  language/ Normative language slices (e.g. Core v0)
  design/   Architecture, optimization, implementation strategy
examples/   Example NEX programs
src/        Compiler implementation
include/    Public/internal C++ headers
runtime/    Future NEX runtime support
tests/      Compiler tests
nex.md      Living project overview and AI/context document
```

## Key Documents

- [LLM_REFERENCE.md](./LLM_REFERENCE.md) - compact index for future chats/LLMs
- [docs/language/core_v0.md](./docs/language/core_v0.md) - normative **Core v0** language (first compiler milestone)
- [docs/user/core_v0_tutorial.md](./docs/user/core_v0_tutorial.md) - beginner guide for writing current NEX programs
- [docs/design/frontend_contract.md](./docs/design/frontend_contract.md) - lexer/parser/AST contract for the first frontend implementation
- [NEXC_HOLY_BOOK.md](./NEXC_HOLY_BOOK.md) - educational guide to the compiler as it grows
- [docs/user/frontend.md](./docs/user/frontend.md) - practical guide for frontend inspection and semantic checking
- [nex.md](./nex.md) - living project overview and language-planning context
- [docs/design/optimization_goals.md](./docs/design/optimization_goals.md) - optimization and static-analysis goals

## Language Direction

NEX is intended to support:

- explicit allocation and copying
- explicit blocking and concurrency
- task/channel-based concurrency
- fixed-size arrays and buffers
- shape-aware linear algebra
- optional resource observability
- realtime-safe function regions
- compiler-visible effect tracking
- low-overhead embedded-oriented profiles

NEX is not intended to initially support:

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

Long term, NEX may become partially or fully self-hosting once the language is mature enough.

## Status

Early-stage. No stable compiler implementation yet.

## License

TBD.
